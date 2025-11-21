# Conversation Helper Example

Demonstrates the conversation helper API for managing multi-turn conversations with automatic message history.

## Features

- System prompts for AI personality
- Automatic message history management
- Multi-turn stateful conversations
- Conversation clearing and reset

## Configuration

Update WiFi and API key in `conversation_example.c`.

## Build and Run

```bash
cd examples/conversation
idf.py build flash monitor
```

## What You'll Learn

- Using `xai_conversation_create()` with system prompts
- Adding user/assistant messages with `xai_conversation_add_user/assistant()`
- Completing conversations with `xai_conversation_complete()`
- Clearing history with `xai_conversation_clear()`
- Managing conversation lifecycle

## Key API Calls

```c
// Create with system prompt
xai_conversation_t conv = xai_conversation_create("You are a helpful assistant");

// Add user message
xai_conversation_add_user(conv, "What is RTOS?");

// Get response (maintains history)
xai_conversation_complete(client, conv, &response);

// Add assistant response to history
xai_conversation_add_assistant(conv, response.content);

// Clear history
xai_conversation_clear(conv);

// Cleanup
xai_conversation_destroy(conv);
```

## Related Examples

- `basic_chat` - Single-turn completions
- `streaming_chat` - Real-time streaming responses

