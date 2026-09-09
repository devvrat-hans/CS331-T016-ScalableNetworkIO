# AI Tools Used

## Overview

Artificial intelligence tools were used as supporting tools throughout the project, mainly for technical learning, code review, report preparation, presentation preparation, LaTeX formatting, benchmark-data interpretation, and iterative review.

The AI tools used were:

1. ChatGPT Go
2. Google Gemini Pro
3. Claude Sonnet

The tools were not treated as authoritative sources. Their outputs were considered suggestions or drafts and were manually reviewed before being incorporated into the final report, LaTeX source, plots, or presentation.

---

## 1. ChatGPT Go

ChatGPT was used for:

- Understanding the overall project requirements.
- Learning the concepts behind single-threaded TCP echo servers.
- Understanding `select`, `poll`, `epoll`, and `io_uring`.
- Understanding the difference between readiness notification and asynchronous completion.
- Understanding scaling concepts such as O(N), O(max_fd), O(ready), and amortized O(1).
- Reviewing source code written by team members.
- Identifying implementation-specific details that were worth mentioning in the report.
- Planning the structure of the technical report.
- Drafting and refining sections of the report.
- Generating and revising LaTeX source.
- Planning the presentation structure and slide content.
- Reviewing the PPT against the required professor-provided format.
- Improving technical explanations and wording.
- Reviewing benchmark observations and helping convert measurements into explanations.

ChatGPT was also used iteratively. Initial outputs were often used as drafts, after which additional prompts were given to add missing information, change the structure, simplify explanations, or correct technical details.

---

## 2. Google Gemini Pro

Gemini was used for similar activities, primarily:

- Learning and cross-checking technical concepts.
- Reviewing explanations of `select`, `poll`, `epoll`, and `io_uring`.
- Reviewing report content.
- Reviewing presentation content and structure.
- Reviewing LaTeX formatting.
- Providing alternative explanations or alternative ways of presenting technical information.

Gemini was particularly useful as a second source of explanation. Where different AI-generated suggestions existed, they were compared manually and only the useful or technically consistent suggestions were retained.

---

## 3. Claude Sonnet

Claude was used for:

- Learning technical concepts.
- Reviewing the project documentation.
- Reviewing report structure and content.
- Reviewing PPT structure and content.
- Reviewing and improving LaTeX.
- Reviewing the benchmark data and helping identify which observations were useful for the report and presentation.
- Suggesting suitable graphs, tables, comparisons, and discussion points based on the benchmark results.

Claude was used more heavily for the benchmarking-analysis stage. The benchmark data and related files were provided to the model, and the model was used to help identify trends, useful comparisons, and ways of communicating the measurements. These suggestions were then checked against the actual benchmark files and results before being used.

---

## General Use of AI

The three tools were used interchangeably for learning and review. The final choice among competing suggestions was made manually.

AI output was therefore used as a combination of:

- Learning assistant
- Technical reviewer
- Drafting assistant
- Structure/planning assistant
- Data-analysis assistant
- Formatting assistant

The final submitted material was reviewed and edited by the student before submission.