# SwitchHub Workflow Template - Beginner's Guide

## Basic Structure

Every workflow is a JSON file with this structure:

```json
{
  "name": "My Workflow Name",
  "description": "What this workflow does",
  "auto_detect_baud": false,
  "steps": [ /* array of step objects */ ]
}
```

---

## Complete Step Template (All Options)

```json
{
  "name": "Step Name",
  "status_text": "What shows in GUI during this step",
  
  "command": "send this command",
  "interrupt": "__BREAK__",
  
  "expect_regex": "wait for this single pattern",
  "expect_any": ["pattern1", "pattern2", "pattern3"],
  
  "on_match": {
    "pattern1": "command to send if pattern1 matches",
    "pattern2": "command to send if pattern2 matches"
  },
  
  "delay_after_match": 0,
  
  "timeout_sec": 10,
  "require_physical_interact": false,
  "hold_interact_timer": 10,
  
  "optional": false,
  "alternate_steps": [ /* array of backup steps */ ]
}
```

---

## 📖 Field Descriptions

### Required Fields:
| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Name of this step (for logging) |
| `status_text` | string | What GUI displays during step |

### Action Fields (pick ONE):
| Field | Type | Description |
|-------|------|-------------|
| `command` | string | Command to send to device |
| `interrupt` | string | Special: `"__BREAK__"` for boot interruption or custom interrupt sequence |

### Response Handling (pick ONE):
| Field | Type | Description |
|-------|------|-------------|
| `expect_regex` | string | Wait for single pattern to match |
| `expect_any` | array | Wait for ANY of these patterns (first match wins) |
| (none) | - | Just wait for timeout period |

### Conditional Logic:
| Field | Type | Description |
|-------|------|-------------|
| `on_match` | object | Send different commands based on which pattern matched. Keys = patterns, Values = commands |
| `delay_after_match` | number | Wait X seconds AFTER pattern matches, BEFORE sending command (default: 0) |

### Timing & Interaction:
| Field | Type | Description |
|-------|------|-------------|
| `timeout_sec` | number | How long to wait (default: 10) |
| `require_physical_interact` | boolean | Pause and prompt user to do something physical (default: false) |
| `hold_interact_timer` | number | How long to pause for physical interaction (default: 10) |

### Fallback & Error Handling:
| Field | Type | Description |
|-------|------|-------------|
| `optional` | boolean | If true, failure won't stop workflow (default: false) |
| `alternate_steps` | array | Backup steps to try if this step fails |

---

### Example 1: Simple Command
```json
{
  "name": "Check Version",
  "status_text": "Checking device version...",
  "command": "show version",
  "timeout_sec": 5
}
```

### Example 2: Wait for Specific Response
```json
{
  "name": "Enter Config Mode",
  "status_text": "Entering configuration mode...",
  "command": "configure terminal",
  "expect_regex": "config",
  "timeout_sec": 5
}
```

### Example 3: Multiple Possible Responses
```json
{
  "name": "Wait for Boot",
  "status_text": "Waiting for device...",
  "expect_any": ["Press RETURN", "login:", ">"],
  "timeout_sec": 60
}
```

### Example 4: Conditional Response
```json
{
  "name": "Handle Boot Prompt",
  "status_text": "Responding to device...",
  "expect_any": ["Press RETURN", "Would you like"],
  "on_match": {
    "Press RETURN": "\r",
    "Would you like": "no"
  },
  "timeout_sec": 10
}
```

### Example 5: Delayed Response
```json
{
  "name": "Wait for Multi-line Prompt",
  "status_text": "Waiting for complete prompt...",
  "expect_any": ["Would you like to enter", "Press RETURN"],
  "on_match": {
    "Would you like to enter": "no",
    "Press RETURN": "\r"
  },
  "delay_after_match": 2,
  "timeout_sec": 30
}
```
**Why use delay_after_match?** Some devices display multi-line prompts slowly, or aren't ready to accept input immediately after showing the prompt. The 2-second delay ensures the complete message is displayed and the device is ready.

### Example 6: Physical Interaction
```json
{
  "name": "Break Into Boot Mode",
  "status_text": "POWER CYCLE THE DEVICE NOW!",
  "require_physical_interact": true,
  "interrupt": "__BREAK__",
  "expect_regex": "rommon",
  "timeout_sec": 60
}
```

### Example 7: Optional Step
```json
{
  "name": "Backup Config (Optional)",
  "status_text": "Attempting backup...",
  "command": "copy running-config tftp://192.168.1.100/backup.cfg",
  "expect_regex": "\\[OK\\]",
  "timeout_sec": 30,
  "optional": true
}
```

### Example 8: With Fallback
```json
{
  "name": "Enter Enable Mode",
  "command": "enable",
  "expect_regex": "Password:",
  "timeout_sec": 5,
  "alternate_steps": [
    {
      "name": "Enable Without Password",
      "command": "enable",
      "expect_regex": "#",
      "timeout_sec": 3
    }
  ]
}
```

---

## 🎓 When to Use delay_after_match

### Use Case 1: Multi-line Prompts
**Problem:** Device shows:
```
Would you like to enter the initial
configuration dialog? [yes/no]:
```
If you respond too quickly, device might only see first line.

**Solution:**
```json
{
  "expect_any": ["configuration dialog"],
  "on_match": {
    "configuration dialog": "no"
  },
  "delay_after_match": 2
}
```

### Use Case 2: Slow Console Output
**Problem:** Device shows prompt but buffer isn't flushed yet.

**Solution:**
```json
{
  "expect_regex": "login:",
  "delay_after_match": 1,
  "command": "admin"
}
```

### Use Case 3: Device Processing Time
**Problem:** Device shows "Ready" but CPU is still busy.

**Solution:**
```json
{
  "expect_regex": "Ready",
  "delay_after_match": 3
}
```

### Use Case 4: TFTP/File Transfer Prompts
**Problem:** Device asks for confirmation but needs time to prepare.

**Solution:**
```json
{
  "expect_any": ["Destination filename", "Overwrite"],
  "on_match": {
    "Destination filename": "\r",
    "Overwrite": "yes"
  },
  "delay_after_match": 1
}
```

---

## Complete Working Example

**File: workflows/my_first_workflow.json**

```json
{
  "name": "Basic Switch Setup",
  "description": "Configure a new Cisco switch",
  "auto_detect_baud": true,
  "steps": [
    {
      "name": "Wait for Device Ready",
      "status_text": "Waiting for switch to boot...",
      "expect_any": ["Press RETURN", "Would you like", ">"],
      "timeout_sec": 120
    },
    {
      "name": "Handle Boot Prompt",
      "expect_any": ["Press RETURN", "Would you like"],
      "on_match": {
        "Press RETURN": "\r",
        "Would you like": "no"
      },
      "delay_after_match": 2,
      "timeout_sec": 10
    },
    {
      "name": "Wait for CLI Prompt",
      "expect_regex": ">",
      "timeout_sec": 10
    },
    {
      "name": "Enter Enable Mode",
      "status_text": "Entering privileged mode...",
      "command": "enable",
      "expect_regex": "#",
      "timeout_sec": 5
    },
    {
      "name": "Enter Configuration",
      "command": "configure terminal",
      "expect_regex": "config",
      "timeout_sec": 5
    },
    {
      "name": "Set Hostname",
      "command": "hostname MySwitch",
      "expect_regex": "MySwitch",
      "timeout_sec": 5
    },
    {
      "name": "Exit Configuration",
      "command": "end",
      "expect_regex": "#",
      "timeout_sec": 5
    },
    {
      "name": "Save Configuration",
      "status_text": "Saving configuration...",
      "command": "write memory",
      "expect_regex": "\\[OK\\]",
      "timeout_sec": 10
    }
  ]
}
```

---

## Checklist

Before running your workflow:
- [ ] All steps have `name` and `status_text`
- [ ] Each step has either `command` OR `interrupt` (not both)
- [ ] Timeouts are reasonable (short for prompts, long for boots/uploads)
- [ ] Regex patterns use `\\` for escaped characters (e.g., `\\[OK\\]` not `[OK]`)
- [ ] Optional steps marked with `"optional": true`
- [ ] Use `delay_after_match` for multi-line prompts or slow devices
- [ ] Tested on non-production device first

---

## 🎓 Pro Tips

1. **Start Simple** - Begin with basic command/expect pairs
2. **Test Each Step** - Build workflow incrementally
3. **Use expect_any** - Devices often show different prompts
4. **Add delay_after_match** - If device responds too fast or shows multi-line prompts
5. **Add Fallbacks Later** - Get main path working first
6. **Check Logs** - Review output to verify patterns match
7. **Escape Special Characters** - Use `\\` for `[`, `]`, `(`, `)`, `.` in regex

---

## 📊 Timing Reference Guide

| Scenario | Recommended Setting |
|----------|-------------------|
| Simple prompt (">", "#") | No delay needed |
| Password prompt | No delay needed |
| Multi-line boot message | `delay_after_match: 2` |
| "Press RETURN to continue" | `delay_after_match: 1` |
| Initial config dialog | `delay_after_match: 2-3` |
| File transfer confirmation | `delay_after_match: 1` |
| Slow console (9600 baud) | `delay_after_match: 1-2` |

---

## 🔍 Log Output with Delay

When using `delay_after_match`, you'll see this in logs:

```
[RX] Would you like to enter the initial
configuration dialog? [yes/no]:
[MATCH] Matched pattern: 'Would you like'
[DELAY] Waiting 2 seconds after match before sending command...
[CONDITIONAL] Matched 'Would you like', sending: no
[TX] no
```

---

**Save as `my_workflow.json` in `workflows/` directory and select it in SwitchHub!** 🚀