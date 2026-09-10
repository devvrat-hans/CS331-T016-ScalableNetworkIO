# Step-by-Step — Where and How AI Contributed

1. **Understanding the project**  
   AI was first used to explain the overall project, its objectives, the four network I/O mechanisms, and the specific work required for the `select` component.

2. **Understanding `select`**  
   AI helped explain what a `select` server is, how `select()` works, how file descriptor sets are managed, and how it can be used to handle multiple TCP clients in an echo server.

3. **Initial implementation**  
   AI was used to provide implementation guidance and basic code for the `select`-based TCP echo server. The code was then understood, modified, and refined as required.

4. **Testing the implementation**  
   AI was used to determine how the server should be compiled and tested and how to verify that it correctly accepts clients, receives messages, and sends the same data back.

5. **Debugging and handling edge cases**  
   During development, AI was used to investigate potential issues in the implementation, including a data-loss problem when a client had pending writes and new data was received. The issue and its possible solution were analyzed before modifying the code.

6. **Code verification**  
   The final `select` implementation was reviewed with AI to identify compilation issues, logical errors, and possible edge cases. The code was also cross-checked using other AI tools and reviewed by teammates.

7. **Consistency with other implementations**  
   The `select` implementation was compared with the other team implementations to ensure that the overall server structure, behavior, and project conventions remained consistent while using the appropriate I/O mechanism.

8. **Documentation**  
   AI was used to prepare and refine the README for the `select` server based on the structure of the team's existing documentation. It was also used to prepare the other Markdown files required for documenting AI usage.

9. **Final validation**  
    After the implementation and supporting files were completed, the code and documentation were reviewed and tested before being integrated into the project repository.