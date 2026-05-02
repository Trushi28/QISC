{
  "version": 1,
  "timestamp": 1777221339,
  "run_count": 1,
  "ir_hash": 0,
  "has_converged": false,
  "functions": [
    {"name": "main", "call_count": 1, "total_cycles": 21283, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "hot_loop_a", "call_count": 1, "total_cycles": 10726, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "mix", "call_count": 6001, "total_cycles": 10450, "is_hot": true, "is_cold": false, "should_inline": true},
    {"name": "hot_loop_b", "call_count": 1, "total_cycles": 10542, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "cold_once", "call_count": 1, "total_cycles": 5, "is_hot": false, "is_cold": true, "should_inline": false}
  ],
  "branches": [
  ],
  "loops": [
    {"location": "hot_loop_a:7", "invocation_count": 1, "total_iterations": 3000, "avg_iterations": 3000.00, "should_unroll": false, "suggested_unroll_factor": 1},
    {"location": "hot_loop_b:15", "invocation_count": 1, "total_iterations": 3000, "avg_iterations": 3000.00, "should_unroll": false, "suggested_unroll_factor": 1}
  ]
}
