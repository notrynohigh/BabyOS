#!/bin/bash
nm -n selftest 2>/dev/null | grep -E "(b_mod_param|param_instance)" | head -20
