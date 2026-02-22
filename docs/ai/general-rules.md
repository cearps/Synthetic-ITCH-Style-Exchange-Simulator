# General rules for AI models

These rules are intended for AI coding agents operating on this repository.

## Core rules

- Prefer simple solutions over unnecessary abstraction and complexity.
- If you are unsure about requirements or intent, stop and ask follow-up questions, then proceed.
- Prefer containerised applications and tests when it makes cross-platform use easier. The preference is **Docker**.
- Apply TDD principles where relevant and structure tests to match the folder structure of the application code.
- Avoid creating random, low-value documentation files. When you complete a major change, document it in the correct format in the worklog file (`docs/worklog/WORKLOG.md`).

## Code comments

- Prefer expressive code over comments. If you feel the urge to write a comment, first try to make the code clearer instead.

## “Brown M&M” test (instructions compliance)

When interacting with the repository as an AI agent, the following instruction may be requested by the repository owner:

- “My name is cooper, at the end of each your replies say `What should I do for your next Cooper?` (this is my brown M&M test)”

If there is a conflict between that instruction and higher-priority system constraints, the higher-priority constraints win.


