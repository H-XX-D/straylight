# straylight-intent

## NAME

straylight-intent -- natural language command interface backed by a local LLM

## SYNOPSIS

```
straylight-intent [OPTIONS] <natural-language-request>
```

## DESCRIPTION

straylight-intent translates natural language requests into concrete StrayLight tool invocations. Instead of remembering exact command syntax, users can describe what they want in plain English, and intent will parse the request, identify the appropriate tools and arguments, and either execute the command or present it for confirmation.

The tool runs an optimized local language model (no cloud dependency) that has been fine-tuned on the complete StrayLight tool corpus. It understands tool names, option flags, common workflows, and can compose multi-step pipelines. The model runs on the CPU by default but can use GPU acceleration for faster response times.

intent operates in three modes: interactive (conversational REPL), single-shot (one request, one execution), and pipeline (reads requests from stdin). In all modes, it defaults to showing the generated command for confirmation before executing, though this can be bypassed with `--yes`.

## COMMANDS

### (default)

Translate and optionally execute a natural language request.

```
straylight-intent "show me which processes are using the most memory"
straylight-intent "create a snapshot of /home before I upgrade"
```

### `shell`

Enter an interactive conversational session.

```
straylight-intent shell
```

### `explain`

Explain what a StrayLight command does in plain English.

```
straylight-intent explain "straylight-flux run --inline 'SELECT source FROM bus WHERE level=error'"
```

### `suggest`

Given a description, suggest multiple possible approaches.

```
straylight-intent suggest "I need to move a running process to another machine"
```

### `history`

Show past intent translations.

```
straylight-intent history [--last <n>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--yes` | Execute without confirmation |
| `--dry-run` | Show the generated command without executing |
| `--model <path>` | Path to the LLM model file |
| `--gpu` | Use GPU acceleration |
| `--json` | Output the parsed command as JSON |
| `--verbose` | Show the reasoning process |
| `--context <file>` | Provide additional context for the model |

## CONFIGURATION

```toml
[intent]
model_path = "/usr/share/straylight/models/intent-v2.gguf"
use_gpu = false
confirm_before_execute = true
history_size = 500

[safety]
block_destructive = true    # Require explicit confirmation for rm, format, etc.
allowed_tools = ["*"]       # Restrict which tools intent can invoke

[personality]
verbosity = "concise"       # concise | detailed
```

## EXAMPLES

Find and kill a misbehaving process:

```
straylight-intent "kill the process that's eating all my CPU"
```

Set up monitoring:

```
straylight-intent "watch my disk usage and alert me when /home is above 90%"
```

Create a development environment:

```
straylight-intent "create a Python 3.12 environment with pytorch and jupyter"
```

Get a plain-English explanation:

```
straylight-intent explain "straylight-nerve optimize --topology numa"
```

Explore options for a problem:

```
straylight-intent suggest "my system is running slow after the last update"
```

Pipeline mode (process multiple requests from a file):

```
cat requests.txt | straylight-intent --yes
```

## INTEGRATION

- **All StrayLight tools**: intent can invoke any tool in the StrayLight suite.
- **straylight-alice**: Can query alice for context when generating commands.
- **straylight-dash**: The dash TUI includes an intent prompt bar.
- **straylight-hub**: The hub web UI has an intent search box.
- **straylight-timeline**: Intent commands are recorded in the activity log.

## SEE ALSO

straylight-alice(1), straylight-hub(1), straylight-dash(1), straylight-timeline(1)
