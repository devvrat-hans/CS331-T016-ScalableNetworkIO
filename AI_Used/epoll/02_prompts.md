# Prompts Given

The following summarizes the main prompts given to AI tools during the development of the `epoll` component. The prompts were used across different stages of the project for learning, implementation, debugging, verification, testing, build configuration, and documentation.

## Claude

1. "Project topic: Scalable Network I/O: From select to io_uring ... Could you elaborate on this project, such that I could understand what do I need to do and how should I proceed, what should I know in detail, means each and every thing."

2. "We have 6 people in group, divide this project into 6 tasks, such that all 6 can work individually without waiting for other, if not possible please tell me how to divide this project. Also give the exact whole description in detail what he needs to do, what he need to use and everything."

3. "I am doing epoll. First you need to explain me what actually is project about, then what things means what here, then why, how, what, wh questions, then we will continue with further things."

4. "Any youtube video you would suggest me, to clear up things a way better?"

5. "Can you suggest me a earlier video, means, what it happening, why there is poll, select and epoll, etc, if you understand what I want can you express and share?"

6. "Give me youtube videos that I would need to do my part."

7. "Next" was used repeatedly to progress through the dry-run trace and the 10-subtask implementation sequence.

8. "Now we get into writing of actual epoll code, Tell the whole task and subtask. We will cover 1 subtask in 1 response..."

9. "Do you remember we need to write it from scratch?"

10. "[Uploaded poll_echo_server.c] This is code for poll, we need to follow all things like attached file for epoll? Did you understand what I am trying to say?"

11. "Start from 1st subtask for epoll" was used repeatedly with "next" through all 10 subtasks.

12. "Give me epoll whole code with minimal appropriate comments just like poll file."

13. "I need to test it, give me testing bash file, so I can test it, double sure the bash file is correct test file."

14. "What is this make file code means? do I need this for epoll too?" followed by a teammate's Makefile.

15. "[Uploaded README.md] Give me README.md for epoll reference poll is attached."

16. "I am not giving any test file."

17. "[Pasted a teammate's code review with specific measured bugs] Is this correct now?" followed by an updated version of the code: "This is updated code. Is this correct now?"

## Gemini and ChatGPT

Gemini and ChatGPT were mainly used for independent verification and fixing specific issues identified during development and review. The prompts were focused on checking whether the updated implementation was correct and whether the flagged issues had been properly resolved.

1. "Check whether the code is correct or not. If there are any issues, list them and explain how to solve them."

2. "Review the updated epoll code and check whether the previously identified problems have been fixed."

3. "Check the code for any remaining logical, compilation, or edge-case issues and suggest the required fixes."