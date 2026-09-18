# syr2k

Method: C. Partitions: 1.

- `source/`: original HLS RTL and its source hierarchy.
- `partition/`: the partitions actually used in this method, exact BLIF and SELF patterns.
- `syr2k_model/`: frozen error models.
- `als_configs/`: portable ALS configurations.
- `simulators/`: rebuildable reference, training and candidate-validation harnesses.
- `candidate_banks/`: recorded candidate features; compressed netlists are in the release assets archive.
