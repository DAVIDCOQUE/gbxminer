# GBXminer Benchmarks

Benchmark conditions:
- GPU temperature measured under load (>80°C, thermal saturation)
- Hashrate measured after warmup period (minimum **900 seconds** / 15 minutes)
- Default kernel launch configs unless specified
- Stock GPU clocks (no OC/undervolt unless noted)

**To submit benchmarks, you MUST run at least 900s.** Shorter runs will not be accepted.
**To run a benchmark, you must use:** `./gbxminer-x.x.x --benchmark -a <algo> --time-limit=900`

---

## GeForce GTX Series

### GTX 1080 Ti
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | 1743.29 kH/s | 84°C | 1755 MHz | 580.126.09 | gbxminer 1.0.1 | 11.72 kH/W | [X]
| NeoScrypt | 1726.46 kH/s | 83°C | 1787 MHz | 580.126.09 | gbxminer 1.1.0 | 11.47 kH/W | [X]

---

## GeForce RTX Series

### RTX 2080 Ti
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | [ ]
| X16R | | | | | | | [ ]
| X16S | | | | | | | [ ]

### RTX 3070
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | [ ]
| X16R | | | | | | | [ ]

### RTX 3080
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | [ ]
| X16R | | | | | | | [ ]

### RTX 3090
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | [ ]
| X16R | | | | | | | [ ]

### RTX 4070
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | [ ]
| X16R | | | | | | | [ ]

### RTX 4080
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | | [ ]
| X16R | | | | | | | | [ ]

### RTX 4090
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | | [ ]
| X16R | | | | | | | | [ ]

### RTX 5070
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | | [ ]
| X16R | | | | | | | [ ]

### RTX 5080
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | | [ ]
| X16R | | | | | | | | [ ]

### RTX 5090
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | | | | [ ]
| X16R | | | | | | | | | [ ]

---

## Quadro / Tesla

### A100
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-------|-----------|-------|
| NeoScrypt | | | | | | | [ ]
| X16R | | | | | | | [ ]

---

## Submit Your Benchmark

**Requirements:**
- Run benchmarks for at least **900 seconds** (15 minutes) to ensure thermal stabilization
- Use `--benchmark -a <algo> --time-limit=900` or longer

To add your results, edit this file to add a row, then open a Pull Request:

```markdown
| Algorithm | Hashrate | Temp | Notes | Driver | Version | Efficiency | Verified?
|------------|----------|------|-------|-------|-----------|-------|-------|
| NeoScrypt | 1700 kH/s | 80°C | 1800 MHz | 535.154 | gbxminer 1.0.1 | ~12.5 kH/W | [X]
```

Include:
- **GPU model** (exact variant if possible)
- **Algorithm** (e.g., neoscrypt, x16r, x11)
- **Hashrate** (use kH/s, GH/s, MH/s, or H/s consistently)
- **Temperature** under load (after 900+ seconds)
- **Notes** (any OC, undervolt, driver version, etc.)
- **Driver** version (e.g., 580.126.09)
- **Version** - gbxminer version
- **Efficiency** - typically kH/W for slower algos, MH/W or GH/s for fast  algos; include units
- **Verified** - use [X] if hash verified against CPU or known good output
