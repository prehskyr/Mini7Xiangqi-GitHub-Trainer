#!/usr/bin/env python3
"""Patch a pinned variant-nnue-tools checkout with Mini7 rules.

The patch is semantic/idempotent so it is less sensitive to upstream line numbers.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one marker, found {count}")
    return text.replace(old, new, 1)


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def patch(root: Path) -> None:
    src = root / "src"

    p = src / "variant.h"
    s = p.read_text(encoding="utf-8-sig")
    s = replace_once(
        s,
        "  ChasingRule chasingRule = NO_CHASING;\n",
        "  ChasingRule chasingRule = NO_CHASING;\n"
        "  int rookCheckLimit = 0; // Consecutive own turns checking with a rook; 0 disables\n",
        "variant.h rookCheckLimit",
    )
    write(p, s)

    p = src / "parser.cpp"
    s = p.read_text(encoding="utf-8-sig")
    s = replace_once(
        s,
        '    parse_attribute("chasingRule", v->chasingRule);\n',
        '    parse_attribute("chasingRule", v->chasingRule);\n'
        '    parse_attribute("rookCheckLimit", v->rookCheckLimit);\n',
        "parser.cpp rookCheckLimit",
    )
    write(p, s)

    p = src / "position.h"
    s = p.read_text(encoding="utf-8-sig")
    s = replace_once(
        s,
        "  Move       move;\n  int        repetition;\n",
        "  Move       move;\n  Piece      movedPiece;\n  int        repetition;\n"
        "  int        rookCheckStreak;\n  bool       rookCheckViolation;\n",
        "position.h state fields",
    )
    s = replace_once(
        s,
        "  int n_fold_rule() const;\n",
        "  int n_fold_rule() const;\n  int rook_check_limit() const;\n",
        "position.h accessor declaration",
    )
    marker = "inline int Position::n_fold_rule() const {\n  assert(var != nullptr);\n  return var->nFoldRule;\n}\n"
    addition = marker + "\ninline int Position::rook_check_limit() const {\n  assert(var != nullptr);\n  return var->rookCheckLimit;\n}\n"
    s = replace_once(s, marker, addition, "position.h accessor definition")
    write(p, s)

    p = src / "variant.cpp"
    s = p.read_text(encoding="utf-8-sig")
    if "Variant* mini7xiangqi_variant()" not in s:
        start = s.find("    Variant* minixiangqi_variant()")
        if start < 0:
            raise RuntimeError("variant.cpp: minixiangqi function not found")
        boundary = s.find("\n#ifdef LARGEBOARDS", start)
        if boundary < 0:
            raise RuntimeError("variant.cpp: insertion boundary not found")
        function = (
            "\n    // Dedicated 7x7 Xiangqi used by Mini7.\n"
            "    Variant* mini7xiangqi_variant() {\n"
            "        Variant* v = minixiangqi_variant()->init();\n"
            "        v->chasingRule = AXF_CHASING;\n"
            "        v->rookCheckLimit = 3;\n"
            "        return v;\n"
            "    }\n"
        )
        s = s[:boundary] + function + s[boundary:]
    s = replace_once(
        s,
        '    add("minixiangqi", minixiangqi_variant());\n',
        '    add("minixiangqi", minixiangqi_variant());\n'
        '    add("mini7xiangqi", mini7xiangqi_variant());\n',
        "variant.cpp registration",
    )
    write(p, s)

    p = src / "position.cpp"
    s = p.read_text(encoding="utf-8-sig")
    s = replace_once(
        s,
        "  si->move = MOVE_NONE;\n",
        "  si->move = MOVE_NONE;\n"
        "  si->movedPiece = NO_PIECE;\n"
        "  si->rookCheckStreak = 0;\n"
        "  si->rookCheckViolation = false;\n",
        "position.cpp initial state",
    )
    if "  Piece pc = moved_piece(m);\n  st->movedPiece = pc;\n" not in s:
        do_move_start = s.find("void Position::do_move(Move m, StateInfo& newSt, bool givesCheck)")
        do_move_end = s.find("void Position::undo_move", do_move_start)
        if do_move_start < 0 or do_move_end < 0:
            raise RuntimeError("position.cpp: do_move function boundaries not found")
        segment = s[do_move_start:do_move_end]
        marker = "  Piece pc = moved_piece(m);\n"
        if segment.count(marker) != 1:
            raise RuntimeError(f"position.cpp moved piece: expected one marker in do_move, found {segment.count(marker)}")
        segment = segment.replace(marker, marker + "  st->movedPiece = pc;\n", 1)
        s = s[:do_move_start] + segment + s[do_move_end:]
    check_marker = (
        "  st->checkersBB = givesCheck ? attackers_to(square<KING>(them), us) & pieces(us) : Bitboard(0);\n"
        "  assert(givesCheck == bool(st->checkersBB));\n"
    )
    check_block = check_marker + (
        "\n  st->rookCheckStreak = 0;\n"
        "  st->rookCheckViolation = false;\n"
        "  if (rook_check_limit() > 0 && givesCheck && type_of(pc) == ROOK)\n"
        "  {\n"
        "      const StateInfo* previousOwnMove = st->previous && st->previous->move != MOVE_NULL\n"
        "                                           ? st->previous->previous\n"
        "                                           : nullptr;\n"
        "      const int previousStreak = previousOwnMove\n"
        "                              && previousOwnMove->move != MOVE_NULL\n"
        "                              && type_of(previousOwnMove->movedPiece) == ROOK\n"
        "                              && previousOwnMove->checkersBB\n"
        "                                  ? previousOwnMove->rookCheckStreak\n"
        "                                  : 0;\n"
        "      st->rookCheckStreak = previousStreak + 1;\n"
        "      st->rookCheckViolation = st->rookCheckStreak >= rook_check_limit();\n"
        "  }\n"
    )
    s = replace_once(s, check_marker, check_block, "position.cpp rook streak")
    s = replace_once(
        s,
        "  newSt.previous = st;\n  st = &newSt;\n\n  st->dirtyPiece.dirty_num = 0;\n",
        "  newSt.previous = st;\n  st = &newSt;\n\n"
        "  st->movedPiece = NO_PIECE;\n"
        "  st->rookCheckStreak = 0;\n"
        "  st->rookCheckViolation = false;\n"
        "  st->dirtyPiece.dirty_num = 0;\n",
        "position.cpp null move",
    )
    immediate = "bool Position::is_immediate_game_end(Value& result, int ply) const {\n\n"
    immediate_new = immediate + (
        "  // The player who delivered the configured consecutive rook checks loses.\n"
        "  if (rook_check_limit() > 0 && st->rookCheckViolation)\n"
        "  {\n"
        "      result = mate_in(ply);\n"
        "      return true;\n"
        "  }\n\n"
    )
    s = replace_once(s, immediate, immediate_new, "position.cpp immediate result")
    write(p, s)

    p = src / "variants.ini"
    s = p.read_text(encoding="utf-8-sig")
    doc = "# rookCheckLimit: consecutive own turns checking with a rook before that player loses [int]\n"
    if doc not in s:
        marker = "# chasingRule: enable chasing rules [ChasingRule] (default: none)\n"
        s = replace_once(s, marker, marker + doc, "variants.ini documentation")
    write(p, s)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    args = parser.parse_args()
    patch(args.checkout.resolve())
    print("Patched variant-nnue-tools for mini7xiangqi.")
