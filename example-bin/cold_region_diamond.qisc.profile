{
  "version": 1,
  "timestamp": 1777204798,
  "run_count": 1,
  "ir_hash": 0,
  "has_converged": false,
  "functions": [
    {"name": "main", "call_count": 1, "total_cycles": 634, "is_hot": true, "is_cold": false, "should_inline": false}
  ],
  "branches": [
    {"location": "main:4", "taken_count": 10000, "not_taken_count": 1, "taken_ratio": 0.9999, "is_predictable": true},
    {"location": "main:7", "taken_count": 1, "not_taken_count": 0, "taken_ratio": 1.0000, "is_predictable": true}
  ],
  "loops": [
    {"location": "main:3", "invocation_count": 1, "total_iterations": 10001, "avg_iterations": 10001.00, "should_unroll": false, "suggested_unroll_factor": 1}
  ]
}
