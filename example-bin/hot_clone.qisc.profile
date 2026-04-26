{
  "version": 1,
  "timestamp": 1777205557,
  "run_count": 1,
  "ir_hash": 0,
  "has_converged": false,
  "functions": [
    {"name": "main", "call_count": 1, "total_cycles": 18121, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "hot_loop", "call_count": 1, "total_cycles": 18105, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "shared", "call_count": 5001, "total_cycles": 8942, "is_hot": true, "is_cold": false, "should_inline": true},
    {"name": "cold_once", "call_count": 1, "total_cycles": 7, "is_hot": false, "is_cold": true, "should_inline": false}
  ],
  "branches": [
  ],
  "loops": [
    {"location": "hot_loop:7", "invocation_count": 1, "total_iterations": 5000, "avg_iterations": 5000.00, "should_unroll": false, "suggested_unroll_factor": 1}
  ]
}
