"""Frozen mapping scripts used by both releases."""
ABC_VARIANTS: dict[str, str] = {
    "map": "st; map; topo; stime",
    "resyn2": "st; resyn2; map; topo; stime",
    "resyn2rs": "st; resyn2rs; map; topo; stime",
    "compress2rs": "st; compress2rs; map; topo; stime",
    # Large-wrapper probes showed that structural don't-care reduction before
    # compress2rs can expose substantial area that a rewrite-only portfolio
    # misses.  Keep all four orderings because their area/delay trade-offs are
    # design dependent; exact and approximate wrappers use this same set.
    "dc2_compress2rs": "st; dc2; compress2rs; map; topo; stime",
    "dch_compress2rs": "st; dch; compress2rs; map; topo; stime",
    "dc2_dch_compress2rs": "st; dc2; dch; compress2rs; map; topo; stime",
    "dc2_compress2rs_twice": (
        "st; dc2; compress2rs; compress2rs; map; topo; stime"
    ),
}
