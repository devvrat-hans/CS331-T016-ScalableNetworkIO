# 03. Thought Process & Workflow Integration

> **Instructions:** Explain how AI was integrated into your workflow, your technical thought process, how you verified AI outputs (e.g., against Linux man pages, compiler diagnostics, testing), and any limitations or errors from AI that you caught and corrected.

## Benchmarking & Profiling
- **Member:** Chirag Agarwalla (Roll No: 24110093)
- **Component:** Benchmarking scripts & performance evaluation

### Workflow & Thought Process:
- *How AI was integrated:* Used AI iteratively for Bash scripting guidance, benchmark design, and understanding profiling tools such as strace and GNU time.
- *Engineering decisions & design rationale:* Kept the benchmark repeatable by using fixed workloads, controlled server startup/shutdown, multiple client workers, and CSV-based result collection.
- *Verification against Linux documentation/tests:* Tested the script on Linux and verified process handling, socket ownership, resource limits, and profiling output during benchmark runs.
- *AI limitations/errors caught and corrected:* AI suggestions were treated as guidance; implementation details were tested manually and corrected whenever something went wrong.
