# Thought Process and Workflow

## Overview

My role in the project was Report and PPT preparation. The team separately handled the four server implementations and benchmarking. I used AI agents throughout my part of the project as learning, analysis, drafting, review, and formatting assistants.

The main approach was not to treat the first AI response as the final answer. I generally followed an iterative process:

`Understand → Ask AI → Check against project material → Revise → Verify → Finalize`

AI was also used in preparing this `AI_Used` documentation itself. The purpose of this file is to document how AI was integrated into my workflow and how I verified the resulting material.

---

## 1. Understanding the Project Requirements

The first stage was to understand exactly what the project expected.

The project required the team to build and compare four single-threaded TCP echo servers using:

- `select`
- `poll`
- `epoll`
- `io_uring`

The project also required benchmarking at increasing connection counts and analysis of throughput, latency, system-call frequency, CPU efficiency, and memory scalability.

I used ChatGPT, Gemini, and Claude to break down the project description into practical requirements for the report and presentation.

The initial questions were mainly educational. I wanted to understand what the professor was actually asking for and what concepts the report needed to demonstrate.

---

## 2. Learning the Technical Concepts

Before writing the report, I used all three AI tools to learn the technical background required to explain the project.

The main concepts covered were:

- Single-threaded TCP echo servers.
- Socket readiness.
- I/O multiplexing.
- `select()`.
- `poll()`.
- `epoll()`.
- `io_uring`.
- Blocking versus non-blocking event loops.
- Readiness notification versus asynchronous completion.
- Descriptor scanning.
- O(N) and O(max_fd) behaviour.
- O(ready) behaviour.
- Batched submission and completion.
- System-call overhead.
- User-to-kernel transitions.
- Scaling with increasing connection counts.

I asked the models to explain these concepts at different levels, especially where the implementation details were difficult to understand.

Using more than one model was useful because different models sometimes explained the same mechanism differently. I compared the explanations and then used the one that was easiest to understand and most consistent with the project.

---

## 3. Understanding the Team's Code

The four server implementations were written by the respective team members.

My responsibility was not to replace those implementations, but I needed to understand them well enough to describe the system correctly in the report and PPT.

I therefore used AI for code review and explanation.

The workflow was:

1. Obtain the implementation from the relevant team member.
2. Provide the source code to the AI tool.
3. Ask what the code is doing and what mechanism-specific details matter.
4. Inspect the explanation against the actual source code.
5. Ask the team member when an implementation detail was unclear.
6. Keep only the details that were actually relevant to the final report or presentation.

This process helped identify implementation-specific points instead of writing only generic descriptions of the Linux APIs.

Examples of details that were useful in the final documentation included the `FD_SETSIZE` limit in the `select` implementation, the blocking behaviour of the event loops, the persistent interest list in `epoll`, and the submission/completion model used by `io_uring`.

---

## 4. Planning the Report

Once the project and implementations were understood, I used AI to decide what a benchmarking report should contain.

The first drafts were broader and included many sections.

I then reviewed them against the actual project requirements and removed or condensed material that did not contribute much to the main benchmarking argument.

The main areas retained were:

- Project motivation.
- Objectives and research questions.
- System design and mechanism overview.
- Experimental setup.
- Benchmark methodology.
- Throughput results.
- Latency results.
- System-call analysis.
- CPU and memory observations.
- Discussion of scaling behaviour.
- Limitations.
- Conclusion.

The aim was to make the report read as a comparison of the four mechanisms rather than as four unrelated implementation descriptions.

---

## 5. Working with the Benchmark Results

Benchmarking was a separate team responsibility, but the resulting data was required for my report and PPT.

I used AI, particularly Claude during this stage, to examine the supplied benchmark material and help determine:

- Which measurements were important.
- Which comparisons were meaningful.
- Which graphs would communicate the results well.
- Which trends should be discussed.
- Which conclusions were supported by the measurements.
- Which claims would be too strong.

The workflow was not to ask AI to invent results. The actual benchmark output was provided to the models and the resulting values were checked against the source data.

This was particularly important because the report contained both the main benchmark measurements and a separate supplementary profiling pass. I needed to preserve that distinction rather than mixing values from different experiments.

---

## 6. Identifying Problems and Improving the Experimental Discussion

As the report was reviewed, several issues became important to the final version.

Examples included:

- Different idle-timeout behaviour between implementations.
- The distinction between total syscall counts and normalized syscall frequency.
- The original client not retaining individual latency samples.
- The need to distinguish the main five-repetition benchmark from supplementary profiling.
- The need to document CPU, memory, and machine information.
- The need to state loopback-only testing as a limitation.
- The need to avoid claiming that `io_uring` is universally faster than `epoll` when the measured throughput did not support that claim.

AI was used to help identify and explain these issues, but the final wording was based on the actual experiment and project files.

The final report therefore presents the syscall reduction of `io_uring` as a strong architectural result while avoiding an unconditional claim of raw-throughput superiority.

---

## 7. Drafting the Report

After the structure was decided, AI was used to generate an initial report draft and later revise individual sections.

The drafting process was iterative.

A typical cycle was:

1. Ask AI to draft a section.
2. Read the generated section.
3. Check whether the section actually matches the project.
4. Compare numerical claims with benchmark data.
5. Compare implementation claims with source code.
6. Ask for revisions where necessary.
7. Manually edit the resulting text.
8. Move to the next section.

This was repeated for the introduction, system design, methodology, results, discussion, limitations, and conclusion.

The AI therefore contributed substantially to drafting, but the final text was not simply copied without review.

---

## 8. Creating and Revising the LaTeX Report

The report was prepared as a LaTeX document.

AI assistance was used for:

- Creating the initial LaTeX structure.
- Formatting sections and subsections.
- Creating tables.
- Placing figures.
- Formatting captions.
- Adjusting page length.
- Simplifying the LaTeX preamble.
- Fixing compilation errors.
- Fixing alignment and formatting problems.

The generated LaTeX was compiled and checked. When a compilation or rendering problem appeared, it was sent back to the AI for diagnosis and correction.

For example, the report generation process included checking missing packages, figure files, compiler compatibility, and table/figure formatting.

The final page count and rendered output were manually checked before submission.

---

## 9. Creating Graphs and Figures

AI was used to decide which benchmark measurements should be visualized.

It was also used to generate plotting code.

The workflow was:

`Actual benchmark data → AI-assisted plotting code → generated graph → manual inspection`

The resulting graphs were checked for:

- Correct values.
- Correct connection counts.
- Correct mechanism names.
- Correct axis labels.
- Correct units.
- Consistency with the source data.
- Consistency between the graph and the written discussion.

The graphs were therefore generated from project data rather than from numbers generated by the AI.

---

## 10. Preparing the PPT

The PPT was developed after the report had a stable set of results and conclusions.

AI was used first to review the existing presentation and then to reorganize it according to the professor's required structure:

1. Problem Statement & Objectives
2. Architecture / Mechanism
3. Extension / Issues Fixed / Evaluation
4. Non-Functional Testing Parameters
5. Challenges Faced

The “may extend one slide” allowance was used to keep the evaluation/results content readable while staying within the six-slide target that was later adopted.

AI helped with:

- Deciding what information could be moved from the report into slides.
- Condensing long explanations.
- Selecting important results.
- Choosing which graphs should be shown.
- Organizing the slides.
- Reviewing readability and information density.

After AI generated versions of the presentation, I made manual changes based on what I thought was appropriate for the final presentation.

---

## 11. Comparing AI Outputs

For many learning and review tasks, I used more than one AI model.

For example, I could ask ChatGPT, Gemini, and Claude to explain a mechanism or review a section.

I then compared the outputs.

The process was:

`Multiple AI responses → compare → verify against source/project → keep useful content`

A response was not accepted simply because multiple models gave a similar answer. The actual code and measurements remained the more important reference.

---

## 12. Verification and Final Decision-Making

I used several forms of verification before accepting AI-assisted material.

### Source-code verification

Implementation descriptions were compared against the actual code.

### Benchmark verification

Numerical claims were compared against the benchmark outputs and result files.

### Team verification

Implementation details were checked with teammates when necessary.

### Documentation verification

Linux documentation and related technical documentation were consulted when needed for concepts or API behaviour.

### Compilation and execution

Generated LaTeX and plotting code were compiled or executed so that obvious technical or formatting errors could be detected.

### Visual review

The final report and PPT were reviewed after rendering rather than relying only on the source files.

### Manual editing

I made the final changes after AI generated or revised the report and PPT. This included deciding what to keep, what to remove, and what wording or presentation style to use.

---

## 13. Final Workflow

The overall workflow for my Report/PPT role can be represented as follows:

```text
Project description
        ↓
Understand requirements with AI
        ↓
Learn select/poll/epoll/io_uring
        ↓
Study team implementations
        ↓
Collect and inspect benchmark outputs
        ↓
Decide what the report needs to show
        ↓
Generate initial report draft
        ↓
Check claims against code and measurements
        ↓
Revise report and LaTeX
        ↓
Generate/verify graphs
        ↓
Review final report
        ↓
Convert important material into PPT
        ↓
Review PPT against required structure
        ↓
Make final manual edits
        ↓
Final Report + PPT
```

---

## 14. Role of AI in the Workflow

The role of AI changed across the project.

At the beginning, AI was used mostly as a learning tool.

During the middle of the project, it became a drafting and analysis assistant.

Later, it was used more as a reviewer and formatting assistant.

A simplified view is:

```text
Early stage:
Learning and understanding

Middle stage:
Drafting, analysis, LaTeX, graphs, report structure

Final stage:
Review, correction, formatting, PPT preparation
```

The important point was that the AI output was treated as a starting point or suggestion. The final material was selected and edited after checking it against the actual project work.

---

## 15. AI Assistance in Preparing This File

This file is itself part of the AI_Used documentation and was also prepared with assistance from AI agents.

AI was used to organize information from the project conversations and convert the workflow into a structured Markdown document.

I reviewed the resulting document to ensure that it describes my actual use of AI and does not add activities that I did not perform.