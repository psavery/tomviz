# Building the Documentation

This directory contains the Sphinx documentation for tomviz.

## Prerequisites

Install the required packages:

```bash
pip install -r requirements.txt
```

## Building the HTML Documentation

### On Linux/macOS:

```bash
make html
```

### On Windows:

```batch
make.bat html
```

The generated HTML documentation will be in `_build/html/`. Open `_build/html/index.html` in your browser to view it.

## Other Build Formats

Sphinx supports many output formats:

- **HTML**: `make html` (most common)
- **PDF**: `make latexpdf` (requires LaTeX)
- **EPUB**: `make epub`
- **Plain text**: `make text`
- **Man pages**: `make man`

Run `make help` to see all available formats.

## Cleaning Build Files

To remove previously built documentation:

```bash
make clean
```

## Project Structure

```
docs/
├── conf.py           # Sphinx configuration
├── index.md          # Main documentation page (Markdown)
├── api/              # API reference
│   ├── modules.md    # API modules index (Markdown)
│   └── dataset.md    # Dataset module documentation (Markdown)
├── requirements.txt  # Python dependencies for building docs
├── Makefile          # Build commands (Linux/macOS)
├── make.bat          # Build commands (Windows)
└── README.md         # This file
```

## Markdown Support

This documentation uses MyST Parser for Markdown support in Sphinx. You can use:

- Standard Markdown syntax
- Sphinx directives with `{directive}` syntax
- Code blocks with syntax highlighting
- Cross-references with `{ref}` syntax

## Updating Documentation

When you modify docstrings in the Python source code, simply rebuild the documentation:

```bash
make clean
make html
```

The documentation will automatically pick up changes from your type annotations and docstrings.
