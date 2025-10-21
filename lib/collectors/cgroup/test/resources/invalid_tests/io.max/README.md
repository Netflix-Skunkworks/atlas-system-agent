# Test files for io.max invalid parsing

This directory contains test files for validating error handling in io.max parsing:

- io.max.duplicate_keys: Contains duplicate key names in a single line
- io.max.empty: Empty file (should be valid - no throttles to process)  
- io.max.incomplete_lines: Lines with incomplete key=value pairs
- io.max.invalid_keys: Lines with incorrect key names (not rbps/wbps/riops/wiops)
- io.max.malformed_pairs: Lines with malformed key=value syntax
- io.max.missing_fields: Lines missing required throttle fields
- io.max.mixed_validity: Mix of valid and invalid lines
- io.max.non_numeric: Non-numeric values (except "max")
- io.max.too_many_fields: Lines with more than expected 5 fields

Expected io.max format: device rbps=value wbps=value riops=value wiops=value
Where value can be a number or "max" for unlimited.