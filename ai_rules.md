-------------------------------------------------- GLOBAL ENGINEERING RULES ------------------------------------------------

# TOKEN OPTIMIZATION RULES
- Never regenerate entire files unless explicitly requested
- Only output changed sections or diffs
- Reuse existing utilities whenever possible
- Avoid repetitive explanations
- Compress context internally
- Prefer concise implementation notes
- Never duplicate logic
- Never output unnecessary boilerplate
- Preserve context windows carefully
- Summarize previous implementation state before continuing

# IMPLEMENTATION RULES
- Build incrementally
- One subsystem at a time
- Never implement unrelated features
- Never assume unstated requirements
- Ask for clarification if ambiguity exists
- Use production-grade patterns only
- Avoid mock implementations
- Avoid placeholders unless explicitly requested
- Favor modular services over monolith logic
- Prefer extensibility over shortcuts

# ERROR PREVENTION RULES
Before generating code:
1. Analyze architecture consistency
2. Analyze dependency graph
3. Analyze scalability risks
4. Analyze async flow correctness
5. Analyze rate limiting concerns
6. Analyze infra-cost implications
7. Analyze token-cost implications
8. Analyze database implications
9. Analyze observability requirements
10. Analyze fault tolerance requirements

# C++ STYLE RULES
All C++ code must conform to STYLE.md. Key rules:
- File names: lowercase with underscores (family_loader.h, not FamilyLoader.h)
- Braces: opening { on same line as preceding token, always
- Bodies on a separate line: must use {} even for one-liners
- Names: types PascalCase, functions/variables snake_case,
  private members end with _, protected members start with _
- Indentation: 4 spaces, no tabs, Unix LF only
- Line limit: 100 characters
- Long signatures: wrap at (, one parameter per line
- Headers: #pragma once only, no ifdef guards

# CODE QUALITY RULES
Always produce:
- strongly typed code
- reusable abstractions
- centralized configs
- defensive programming
- structured logging
- retry handling
- queue-safe patterns
- async-safe implementations
- testable modules
- environment-safe code
- scalable architecture
- low-coupling services

# OUTPUT RULES
Always respond with:
1. Objective
2. Scope boundaries
3. Architecture considerations
4. Files affected
5. Implementation plan
6. Code changes
7. Validation checklist
8. Risks
9. Next recommended step Never skip validation.

-------------------------------------------------- ARCHITECTURE MEMORY --------------------------------------------------

1. Always obey:
- architecture.md
- decisions.md
- contracts.md
- services.md
- infra.md
2. Do not violate established architecture decisions.
3. If conflicts exist:
- explain the conflict
- propose the lowest-risk solution
- request approval before proceeding

-------------------------------------------------- STRICT FILE BOUNDARIES -----------------------------------------------

1. You may ONLY modify files relevant to the current phase.
2. Never modify unrelated systems.
3. If additional files are needed:
- explain why
- request approval first 

-------------------------------------------------- PHASED DEVELOPMENT EXECUTION MODEL ------------------------------------

1. You MUST follow phased development.
2. Never skip phases.
3. Never build future phases prematurely.
4. After each phase:
- STOP
- validate implementation
- wait for approval before continuing
