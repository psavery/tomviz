# tomviz Documentation

Welcome to tomviz's documentation. This documentation describes the public API
for working with tomviz Dataset objects.

```{toctree}
---
maxdepth: 2
caption: Contents
---
api/modules
```

## Getting Started

The Dataset object is the standard interface for working with image stacks and
volumetric data in tomviz. It provides access to scalar fields, spacing
information, tilt series metadata, and calibration data.

Example usage:

```python
# Access the active scalar field
data = dataset.active_scalars

# Get spacing information
spacing = dataset.spacing

# Access a named scalar field
scalars = dataset.scalars('my_field')

# Add a new scalar field
dataset.set_scalars('processed', processed_data)
```

## Indices and tables

* {ref}`genindex`
* {ref}`modindex`
* {ref}`search`
