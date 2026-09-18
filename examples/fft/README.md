# fft

Method: C. Partitions: 16.

- `source/`: original HLS RTL and its source hierarchy.
- `partition/`: the partitions actually used in this method, exact BLIF and SELF patterns.
- `fft_model/`: frozen error models.
- `als_configs/`: portable ALS configurations.
- `simulators/`: rebuildable reference, training and candidate-validation harnesses.
- `candidate_banks/`: recorded candidate features; compressed netlists are in the release assets archive.
