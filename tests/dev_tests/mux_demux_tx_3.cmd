--adapter EFA --local_ip 10.0.0.212
--log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG
--stderr

# ----------------------------------------------------------------------------
# Non-demux test using legacy APIs. Single TX to single Rx. Use a single Rx
# cdi_test listening on single port.
# ----------------------------------------------------------------------------
-XS -name TX1 --num_transactions 100 --tx AVM --keep_alive --rate 60
-S --id 3 --remote_ip 10.0.0.214 --dest_port 3100 --payload_size 50 --pattern INC --pat_start 1 --avm_anc
