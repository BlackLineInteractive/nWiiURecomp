<p align="center">
  <img src="image/logo_wide.jpg" alt="NWiiRecomp" width="700"/>
</p>

<p align="center">
  Static recompilation and runtime toolkit for Nintendo Wii U executables.
</p>

<p align="center">
(The first recompiler that really works)
</p>

<p align="center">
  <a href="https://discord.gg/wp7zdxyqT">
    <img src="https://img.shields.io/badge/Discord-nWiiURecomp-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="nWiiURecomp Discord"/>
  </a>
  <br><br>
  <a href="https://youtube.com/@blacklineinteractive">
    <img src="https://img.shields.io/badge/YouTube-Blackline_Interactive-FF0000?style=flat-square&logo=youtube&logoColor=white" alt="Blackline Interactive YouTube"/>
  </a>
</p>

---

## What is this?

nWiiURecomp translates Nintendo Wii U (`.rpx`, `.rpl`) executables into native C++ code. The output is a standalone executable that runs natively without instruction-level emulation. Hardware interactions are handled by a High-Level Emulation (HLE) runtime layer.

> **A Note on Our Approach:** nWiiURecomp features a fully **custom-built** rasterizer, shader generator, and HLE runtime tailored specifically for Cafe OS and the Latte GPU.

---

## Project Structure

```
nWiiURecomp/
├── nWiiUAnalyzer/   — RPX/RPL parser and function boundary analyzer
├── nWiiURecomp/     — Offline static recompiler (PPC → C++)
├── nWiiURuntime/    — Cross-platform runtime + Cafe OS / Latte GPU HLE library
└── nWiiUStudio/     — GUI debugging and inspection tool (Raylib + ImGui)
```

---

## What Works

### Analyzer (`nWiiUAnalyzer`)

- RPX section parsing.
- Disassembly and function boundary discovery.

### Recompiler (`nWiiURecomp`)

- Translates PowerPC 750CL instructions to C++.
- Tail-call detection and `goto`-based local branch inlining.

### Runtime (`nWiiURuntime`)

- **Cafe OS**: Initial RPX loading, ELF parsing.
- **Latte GPU**: PM4 packet handling stub.
