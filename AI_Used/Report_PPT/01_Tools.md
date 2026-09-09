# AI Tools Used

## Overview

For the Report role of the T016 – Scalable Network I/O project, I used multiple AI tools as assistants during the learning, analysis, writing, formatting, and review stages.

The main AI tools used were:

1. ChatGPT Go
2. Google Gemini Pro
3. Claude Sonnet

The tools were used throughout the report-preparation process rather than for only one isolated task. I used more than one model for several tasks so that I could compare explanations and approaches and then manually choose what was useful and technically correct.

The final report, LaTeX source, figures, and supporting documentation were not accepted directly from AI output without review. AI-generated material was treated as a draft or suggestion and was checked against the project files, source code, benchmark outputs, project requirements, and feedback from teammates.

---

## 1. ChatGPT Go

ChatGPT was used for a wide range of activities related to my Report and PPT responsibilities.

### Main uses

- Understanding the overall project requirements.
- Breaking the project description into report and presentation requirements.
- Learning the basic and detailed concepts behind:
  - single-threaded TCP echo servers
  - `select`
  - `poll`
  - `epoll`
  - `io_uring`
  - readiness notification
  - asynchronous completion queues
  - O(N), O(max_fd), O(ready), and amortized O(1) behaviour
  - system-call overhead and user/kernel transitions
- Reviewing source code supplied by team members to understand what each implementation actually did.
- Identifying implementation-specific details that were useful to mention in the report.
- Planning the structure of the technical report.
- Generating and revising report content.
- Converting the report into LaTeX and fixing LaTeX-related issues.
- Reviewing the report against the project requirements.
- Planning and restructuring the PPT according to the format specified by the professor.
- Reviewing PPT content for technical completeness and presentation flow.
- Suggesting suitable ways to explain the four I/O mechanisms without relying on code-level details in the presentation.
- Reviewing benchmark observations and helping interpret what the measured results meant for the report and presentation.
- Suggesting graphs, tables, and visual comparisons.
- Generating plotting code that was then used with the project's actual data.
- Reviewing formatting, wording, page count, and overall organization.

ChatGPT was often used iteratively. For example, an initial draft could be generated first, followed by additional prompts asking what was missing, what should be removed, whether a claim was supported by the data, or how the material could be reorganized.

---

## 2. Google Gemini Pro

Gemini was also used for learning, technical review, report preparation, and cross-checking.

### Main uses

- Learning the project concepts from the basics.
- Obtaining alternative explanations of `select`, `poll`, `epoll`, and `io_uring`.
- Understanding the differences between the four event-handling approaches.
- Reviewing technical explanations before using them in the report or PPT.
- Reviewing report organization and content.
- Reviewing PPT structure and content.
- Helping with LaTeX formatting and revisions.
- Reviewing project-specific observations and implementation details.
- Helping interpret project outputs when preparing the documentation.
- Providing a second opinion when another AI model had already produced an explanation or draft.

Gemini was particularly useful for cross-checking explanations and for going through the overall project workflow. I compared its suggestions with the actual project material instead of assuming that its output was automatically correct.

---

## 3. Claude Sonnet

Claude Sonnet was used for the same general purpose, with more emphasis on detailed document review and analysis of the material used in the report.

### Main uses

- Learning technical concepts.
- Understanding the four socket-handling mechanisms.
- Reviewing the team's source code and identifying implementation-specific details.
- Reviewing and improving report structure.
- Reviewing the report against the project description and expected outcomes.
- Reviewing LaTeX source and helping resolve formatting or compilation problems.
- Reviewing the PPT and suggesting improvements.
- Examining the benchmark output supplied by the benchmarking part of the team and helping identify:
  - important trends
  - meaningful comparisons
  - useful tables
  - suitable graphs
  - observations that should be discussed in the report
- Helping distinguish between conclusions supported directly by measurements and conclusions that would be too strong.
- Helping prepare plotting code for figures based on the provided results.

For the benchmark-related part, I mainly used Claude to help examine the data and decide how the measurements should be represented in the report and PPT. The benchmarking itself was a separate team responsibility; my role was to use the resulting data appropriately in the report and presentation.

---

## 4. Use of Multiple AI Tools

I did not rely on a single AI model as the sole source of information.

For learning and technical questions, I used all three tools at different points. When useful, I compared the responses and selected the explanation that was clearest and most consistent with the actual implementation.

For report and PPT preparation, the workflow was generally:

`AI suggestion → manual review → comparison with project material → revision → final selection`

This was important because a technically correct explanation in general terms could still be inaccurate for the particular implementation used by the team.

---

## 5. Verification of AI Output

AI-generated material was checked against the actual project wherever possible.

The checks included:

- Comparing technical explanations with the team's source code.
- Checking benchmark numbers against the actual benchmark output files.
- Asking teammates to confirm implementation-specific details when necessary.
- Compiling and checking generated LaTeX.
- Checking that plots were generated from the actual project data.
- Checking graph labels, values, and interpretations.
- Comparing the final report and PPT with the professor's requirements.
- Correcting or removing statements when they did not accurately describe the implementation or measurements.
- Choosing manually which AI suggestions should be retained, modified, or rejected.

The final report contains project-specific experimental details and limitations rather than relying only on generic explanations. For example, the report distinguishes between the main five-repetition benchmark and the separate profiling measurements and documents the fairness adjustments made before the final comparison.

---

## 6. AI Contribution to the Final Artifacts

AI was used to generate or substantially assist with several project artifacts, including:

- report drafts
- report sections
- LaTeX source
- LaTeX revisions and formatting
- PPT structure
- PPT content
- PPT revisions
- plotting code
- technical explanations
- review comments and suggested corrections

After AI-generated versions were produced, I made the final changes based on the project requirements, the actual results, my understanding of the material, and the level of detail and presentation style I wanted to submit.

---

## 7. AI Assistance in Creating This Documentation

The `AI_Used` Markdown documentation itself was also prepared with assistance from AI agents.

The AI tools were used to organize the information from the project conversations, identify the major stages of AI usage, summarize the roles of the different tools, and format the disclosure into Markdown files.

The information in these files was still manually reviewed before being treated as the final submission. The purpose of the AI assistance here was to organize and document the actual workflow, not to change what tools were used or what work was performed.

---

## Summary

ChatGPT Go, Gemini Pro, and Claude Sonnet were used as supporting tools throughout the Report/PPT workflow for learning, technical understanding, source-code review, benchmark-result interpretation, drafting, LaTeX preparation, plotting, presentation preparation, and review.

The three models were used as assistants rather than as unquestioned sources. Their outputs were compared, checked against the project material, and manually edited before being included in the final work.