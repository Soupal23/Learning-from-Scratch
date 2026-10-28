## 1. Dekker vs Peterson
Dekker's algorithm and Peterson's algorithm solve the same mutual exclusion problem for two processes using flags and a turn variable. The difference is in how conflict is handled.
Peterson's algorithm: The process stays interested and uses turn to give priority if both processes want to enter.

Dekker's algorithm: The process may lower its own flag, wait, and then raise it again. 

```mermaid
flowchart TD
    A["Raise Interest<br/>flag[i] = true"] --> B{"Does the other<br/>process want CS?"}

    B -->|No| C["Execute Critical Section"]
    B -->|Yes| D{"Whose turn is it?"}

    D -->|My turn| C
    D -->|Other's turn| E["Withdraw Interest<br/>flag[i] = false"]

    E --> F["Wait<br/>while turn == other"]
    F --> G["Retry<br/>flag[i] = true"]

    G --> B

    C --> H["Exit CS<br/>turn = other<br/>flag[i] = false"]
```

Peterson's algorithm is more compact and symmetric. Dekker's algorithm handles conflict through multiple steps: withdraw, wait, and retry.