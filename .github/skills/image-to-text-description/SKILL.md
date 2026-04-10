---
name: image-to-text-description
description: '**WORKFLOW SKILL** — Read images and generate detailed text descriptions for use in prompts. USE FOR: converting visual content to textual descriptions, enhancing prompts with image context, generating alt text. DO NOT USE FOR: general image processing, OCR text extraction, non-prompt related descriptions. INVOKES: view_image tool for image analysis, text generation.'
---

# Image to Text Description

## Overview
This skill provides a workflow for analyzing images and generating detailed textual descriptions that can be incorporated into prompts, documentation, or other text-based contexts.

## Workflow Steps

1. **Image Acquisition**: Use the `view_image` tool to load and analyze the target image file.

2. **Description Generation**: Generate a comprehensive text description that captures:
   - Overall scene or subject
   - Key visual elements and their relationships
   - Colors, lighting, and mood
   - Any text or symbols present
   - Context clues for the image's purpose

3. **Prompt Integration**: Format the description in a way that's suitable for inclusion in prompts, such as:
   - Clear, concise language
   - Structured format if needed
   - Focus on elements relevant to the intended use

## Quality Criteria
- Descriptions should be objective and factual
- Include relevant details without unnecessary verbosity
- Maintain neutrality unless specific tone is requested
- Ensure descriptions are standalone and don't require the image to be understood

## Example Usage
When given an image of a flowchart, generate: "A flowchart diagram showing the process flow from input to output, with decision points marked by diamonds and rectangular process steps connected by arrows."</content>
<parameter name="filePath">/home/david/VS Code Projects/Books/.github/skills/image-to-text-description/SKILL.md