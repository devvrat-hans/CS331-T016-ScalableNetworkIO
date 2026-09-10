# Prompts Given

The following summarizes the main prompts given to AI tools during the development of the `select` component. The prompts were used across different stages of the project for learning, implementation, debugging, verification, and documentation.

## Gemini

1. "I have this project on Scalable Network I/O: From select to io_uring. I have been assigned the select part. Please explain the entire project in detail, what we are building, what I need to do, and how I should proceed."

2. "I understand that we are building echo servers and I am implementing the select server. What is a select server, what does it do, and how does it work? Also give me some basic code with a short explanation."

3. "How should I check whether the select server code is correct and working properly? Should I directly run it, and how should I test it?"

4. "[Pasted a possible issue in the select implementation about a client's pending write being overwritten when new data is read.] Is this a valid issue? Please explain the problem and how it should be handled."

5. "This is the final select code. [Pasted code] Please check whether it is correct and whether there are any issues."

6. "There is a Makefile in the main branch. What is a Makefile, why is it needed, and what do I need to write in it for my select server?"

7. "I need to write the Makefile only for my select server, not for all the servers. [Pasted the poll Makefile] Please give me the corresponding Makefile for select."

8. "I am adding the epoll and io_uring implementations. [Added code/files] Please check whether they are correct and whether they follow the same overall structure as my select implementation and the poll implementation."

9. "Please help me write the README for the select server. [Added the poll README as a reference]"

## Claude and ChatGPT

Claude was mainly used for independent code verification and resolving technical issues. The prompts were random and were not part of a single chat. However, ChatGPT was mainly used for some basic and random doubts that were directly or indirectly related to the project.

1. "Check whether the code is correct or not. If there are any issues, list them and also explain how to solve them."

2. "The select code is working and the port should remain 9090 for testing. There is also feedback about the 1-second timeout causing idle wakeups and affecting syscall counts. Do I need to make any changes to my code?"

3. "This is the updated code my friend gave me. [Pasted code] Only tell me if there is anything wrong with it."