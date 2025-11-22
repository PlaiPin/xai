# Tools (Function Calling) Example

Demonstrates client-side tool/function calling where the AI model suggests tools to execute, and your ESP32 executes them locally.

## Features

- Define custom tools/functions
- AI decides when to call tools
- Execute tools on ESP32
- Return results to AI
- Multi-turn tool orchestration

## Configuration

Update WiFi and API key in `tools_example.c`.

## Build

```bash
cd examples/tools
idf.py build flash monitor
```

## What You'll Learn

- Defining tools with JSON schemas
- Setting up tool options
- Detecting tool calls in responses
- Executing tools locally
- Sending tool results back to model

## Key API Calls

```c
// Define tools
xai_tool_t tools[] = {
    {
        .type = "function",
        .function = {
            .name = "get_temperature",
            .description = "Get ESP32 temperature",
            .parameters = "{\"type\":\"object\",\"properties\":{}}"
        }
    }
};

// Enable tools
xai_options_t options = xai_options_default();
options.tools = tools;
options.tool_count = 1;
options.tool_choice = "auto";

// Check for tool calls
if (response.tool_call_count > 0) {
    // Execute each tool
    char *result = execute_tool(response.tool_calls[0].function.name);
    
    // Send result back
    xai_message_t tool_msg = {
        .role = XAI_ROLE_TOOL,
        .content = result,
        .tool_call_id = response.tool_calls[0].id
    };
}
```

## Configuration

Enable tools in menuconfig:
```
Component config → xAI Configuration → Feature Toggles → Enable tool calling support
```

## Tool Execution Flow

1. User asks question
2. AI determines tools needed
3. ESP32 executes tools locally
4. Results sent back to AI
5. AI synthesizes final answer

## Related Examples

- `web_search` - Server-side search tools
- `conversation` - Multi-turn with tools

