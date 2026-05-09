# Boards

This directory defines hardware and VM targets independently from products.

Use a board definition to describe:

- architecture
- boot flow
- firmware expectations
- storage or image format constraints
- default emulator or hardware profile

Products should reference a board instead of hardcoding these details into ad hoc scripts.
