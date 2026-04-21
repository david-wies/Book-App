# Available Agents

This document lists all available custom agents in this repository, along with their descriptions and purposes.

## Agent List

| Agent Name | Description | File |
|------------|-------------|------|
| Thinking Beast Mode | A transcendent coding agent with quantum cognitive architecture, adversarial intelligence, and unrestricted creative freedom. | `Thinking-Beast-Mode.agent.md` |
| Context Architect | An agent that helps plan and execute multi-file changes by identifying relevant context and dependencies | `context-architect.agent.md` |
| Critical Thinking | Challenge assumptions and encourage critical thinking to ensure the best possible solution and outcomes. | `critical-thinking.agent.md` |
| Custom Agent Foundry | Expert at designing and creating VS Code custom agents with optimal configurations | `custom-agent-foundry.agent.md` |
| Debug | Debug your application to find and fix a bug | `debug.agent.md` |
| Expert C++ Software Engineer | Provide expert C++ software engineering guidance using modern C++ and industry best practices. | `expert-cpp-software-engineer.agent.md` |
| Database Specialist | Database specialist with deep experience in table-based and relational database design, schema modeling, query tuning, and migration planning. | `database-specialist.agent.md` |
| MVP to Project Designer | Use when transforming MVP specifications (markdown, JSON, diagrams, images) into software architecture designs, considering programming language, tools, and operating system. | `mvp-to-project-designer.agent.md` |
| Simple App Idea Generator | Brainstorm and develop new application ideas through fun, interactive questioning until ready for specification creation. | `simple-app-idea-generator.agent.md` |
| Specification | Generate or update specification documents for new or existing functionality. | `specification.agent.md` |

## How to Use Agents

To invoke an agent, use the `/` command in GitHub Copilot Chat followed by the agent name, or select from the agent picker.

Example: `/specification` to start the specification agent.

## Adding New Agents

To add a new agent:
1. Create a new `.agent.md` file in the `.github/agents/` directory
2. Follow the standard frontmatter format with `name`, `description`, and optional `tools`
3. Add the agent to this AGENTS.md file
4. Test the agent configuration

## Agent Guidelines

- Each agent should have a clear, focused purpose
- Descriptions should be concise but informative
- Tools should be specified if the agent requires specific capabilities
- Agents should follow the established patterns and best practices