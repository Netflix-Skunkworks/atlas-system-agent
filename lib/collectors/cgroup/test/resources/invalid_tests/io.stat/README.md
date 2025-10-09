# Invalid IO.stat Test Files

This directory contains various invalid io.stat files for testing error handling and validation logic.

## Test Cases

1. **io.stat.missing_fields** - Lines with fewer than 6 required key=value pairs
2. **io.stat.invalid_keys** - Misspelled or incorrect key names (e.g., "readbytes" instead of "rbytes", "wbtyes" instead of "wbytes")
3. **io.stat.non_numeric** - Non-numeric values for statistics (e.g., "invalid", "not_a_number", "abc", "xyz")
4. **io.stat.malformed_pairs** - Missing equals signs in key=value pairs
5. **io.stat.too_many_fields** - More than the expected 7 fields per line
6. **io.stat.negative_values** - Negative numbers for IO statistics (which should be non-negative)
7. **io.stat.empty** - Completely empty file
8. **io.stat.mixed_validity** - Mix of valid and invalid lines
9. **io.stat.duplicate_keys** - Same key appearing multiple times on one line
10. **io.stat.incomplete_lines** - Lines with only device names or insufficient statistics

## Expected Format

Valid io.stat lines should have the format:
```
device_id rbytes=N wbytes=N rios=N wios=N dbytes=N dios=N
```

Where:
- `device_id`: Device identifier (e.g., "259:4")
- `rbytes`: Read bytes
- `wbytes`: Write bytes  
- `rios`: Read I/O operations
- `wios`: Write I/O operations
- `dbytes`: Discard bytes
- `dios`: Discard I/O operations

All values (N) should be non-negative integers.