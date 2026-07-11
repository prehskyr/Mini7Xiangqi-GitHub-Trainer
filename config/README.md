# Variant configuration

`variants.ini` defines the `mini7xiangqi` Fairy-Stockfish variant used as the teacher search engine.

The client/CLI rule implementation remains authoritative for history-dependent adjudication such as consecutive rook checks, perpetual check, chase recognition, and threefold repetition. Those rules cannot be represented completely by the static Fairy-Stockfish variant file alone.
