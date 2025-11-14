#!/usr/bin/env python3
"""
Generate Sphinx documentation for tomviz operators from JSON files.
This script can work with both local JSON files and fetch them from GitHub.
"""

import json
import os
import sys
from pathlib import Path
from typing import Dict, List, Any, Optional
import urllib.request
import urllib.error


def fetch_github_json(user: str, repo: str, branch: str, path: str) -> List[Dict[str, Any]]:
    """
    Fetch JSON files from a GitHub repository.
    
    :param user: GitHub username
    :param repo: Repository name
    :param branch: Branch name
    :param path: Path to directory containing JSON files
    :return: List of parsed JSON objects with metadata
    """
    # First, try to get the directory listing via GitHub API
    api_url = f"https://api.github.com/repos/{user}/{repo}/contents/{path}?ref={branch}"
    
    try:
        with urllib.request.urlopen(api_url) as response:
            files_list = json.loads(response.read())
    except urllib.error.HTTPError as e:
        print(f"Error fetching directory listing: {e}")
        print(f"URL: {api_url}")
        return []
    
    operators = []
    
    for file_info in files_list:
        if file_info['name'].endswith('.json'):
            # Fetch the raw content
            raw_url = file_info['download_url']
            try:
                with urllib.request.urlopen(raw_url) as response:
                    operator_data = json.loads(response.read())
                    operator_data['_filename'] = file_info['name']
                    operators.append(operator_data)
                    print(f"Fetched: {file_info['name']}")
            except (urllib.error.HTTPError, json.JSONDecodeError) as e:
                print(f"Error fetching {file_info['name']}: {e}")
    
    return operators


def load_local_json_files(directory: str) -> List[Dict[str, Any]]:
    """
    Load JSON files from a local directory.
    
    :param directory: Path to directory containing JSON files
    :return: List of parsed JSON objects with metadata
    """
    operators = []
    json_dir = Path(directory)
    
    if not json_dir.exists():
        print(f"Directory not found: {directory}")
        return []
    
    for json_file in json_dir.glob('*.json'):
        try:
            with open(json_file, 'r') as f:
                operator_data = json.load(f)
                operator_data['_filename'] = json_file.name
                
                # Try to load corresponding Python file
                py_file = json_file.with_suffix('.py')
                if py_file.exists():
                    with open(py_file, 'r') as pf:
                        operator_data['_python_code'] = pf.read()
                
                operators.append(operator_data)
                print(f"Loaded: {json_file.name}")
        except (json.JSONDecodeError, IOError) as e:
            print(f"Error loading {json_file.name}: {e}")
    
    return operators


def format_parameter(param: Dict[str, Any]) -> str:
    """Format a parameter dictionary into Markdown."""
    lines = []
    
    # Parameter name and type
    param_name = param.get('name', 'unknown')
    param_type = param.get('type', 'unknown')
    lines.append(f"#### `{param_name}` (*{param_type}*)")
    lines.append("")
    
    # Label
    if 'label' in param:
        lines.append(f"**Label:** {param['label']}")
        lines.append("")
    
    # Description
    if 'description' in param:
        lines.append(param['description'])
        lines.append("")
    
    # Default value
    if 'default' in param:
        lines.append(f"- **Default:** `{param['default']}`")
    
    # Min/Max for numeric types
    if 'minimum' in param:
        lines.append(f"- **Minimum:** `{param['minimum']}`")
    if 'maximum' in param:
        lines.append(f"- **Maximum:** `{param['maximum']}`")
    
    # Precision for doubles
    if 'precision' in param:
        lines.append(f"- **Precision:** {param['precision']} decimal places")
    
    # Step size
    if 'step' in param:
        lines.append(f"- **Step:** `{param['step']}`")
    
    # Options/Enum values
    if 'options' in param and param['options']:
        lines.append("- **Options:**")
        for option in param['options']:
            if isinstance(option, dict):
                for label, value in option.items():
                    lines.append(f"  - `{label}` → `{value}`")
            else:
                lines.append(f"  - `{option}`")
    
    # File filter
    if 'filter' in param:
        lines.append(f"- **File filter:** {param['filter']}")
    
    # Visibility conditions
    if 'visible_if' in param:
        lines.append(f"- **Visible if:** `{param['visible_if']}`")
    
    # Enable conditions
    if 'enable_if' in param:
        lines.append(f"- **Enabled if:** `{param['enable_if']}`")
    
    lines.append("")
    return '\n'.join(lines)


def format_result(result: Dict[str, Any]) -> str:
    """Format a result dictionary into Markdown."""
    lines = []
    
    result_name = result.get('name', 'unknown')
    result_type = result.get('type', 'unknown')
    lines.append(f"- **`{result_name}`** (*{result_type}*)")
    
    if 'label' in result:
        lines.append(f"  - Label: {result['label']}")
    
    lines.append("")
    return '\n'.join(lines)


def format_child(child: Dict[str, Any]) -> str:
    """Format a child dataset dictionary into Markdown."""
    lines = []
    
    child_name = child.get('name', 'unknown')
    child_type = child.get('type', 'unknown')
    child_label = child.get('label', child_name)
    
    lines.append(f"- **{child_label}** (`{child_name}`, type: `{child_type}`)")
    
    if 'description' in child:
        lines.append(f"  - {child['description']}")
    
    lines.append("")
    return '\n'.join(lines)


def extract_function_signature(python_code: str) -> Optional[str]:
    """Extract the main transform function signature from Python code."""
    import re
    
    # Look for def transform( or def transform_scalars(
    pattern = r'def (transform(?:_scalars)?)\s*\((.*?)\):'
    match = re.search(pattern, python_code, re.DOTALL)
    
    if match:
        func_name = match.group(1)
        params = match.group(2)
        # Clean up the parameters
        params = re.sub(r'\s+', ' ', params).strip()
        return f"{func_name}({params})"
    
    return None


def generate_operator_markdown(operator: Dict[str, Any]) -> str:
    """Generate Markdown documentation for a single operator."""
    lines = []
    
    # Title
    name = operator.get('name', 'Unknown Operator')
    label = operator.get('label', name)
    lines.append(f"## {label}")
    lines.append("")
    
    # Description
    description = operator.get('description', 'No description available.')
    lines.append(description)
    lines.append("")
    
    # External compatibility note
    if 'externalCompatible' in operator and not operator['externalCompatible']:
        lines.append("```{note}")
        lines.append("This operator is not compatible with external Python execution.")
        lines.append("```")
        lines.append("")
    
    # Python function signature
    if '_python_code' in operator:
        signature = extract_function_signature(operator['_python_code'])
        if signature:
            lines.append("### Function Signature")
            lines.append("")
            lines.append("```python")
            lines.append(signature)
            lines.append("```")
            lines.append("")
    
    # Parameters section
    if 'parameters' in operator and operator['parameters']:
        lines.append("### Parameters")
        lines.append("")
        for param in operator['parameters']:
            lines.append(format_parameter(param))
    
    # Results section
    if 'results' in operator and operator['results']:
        lines.append("### Results")
        lines.append("")
        lines.append("This operator produces the following results:")
        lines.append("")
        for result in operator['results']:
            lines.append(format_result(result))
    
    # Child datasets section
    if 'children' in operator and operator['children']:
        lines.append("### Child Datasets")
        lines.append("")
        lines.append("This operator creates the following child datasets:")
        lines.append("")
        for child in operator['children']:
            lines.append(format_child(child))
    
    # Python script reference
    filename = operator.get('_filename', '')
    if filename:
        py_filename = filename.replace('.json', '.py')
        lines.append("### Implementation")
        lines.append("")
        lines.append(f"**Python script:** `{py_filename}`")
        lines.append("")
        lines.append(f"**JSON descriptor:** `{filename}`")
        lines.append("")
    
    lines.append("---")
    lines.append("")
    
    return '\n'.join(lines)


def categorize_operators(operators: List[Dict[str, Any]]) -> Dict[str, List[Dict[str, Any]]]:
    """Categorize operators based on their names or labels."""
    categories = {
        'Reconstruction': [],
        'Alignment': [],
        'Filtering': [],
        'Segmentation': [],
        'Transformation': [],
        'Analysis': [],
        'Utilities': [],
    }
    
    for op in operators:
        name = op.get('name', '').lower()
        label = op.get('label', '').lower()
        
        # Simple categorization based on keywords
        categorized = False
        
        if any(kw in name or kw in label for kw in ['recon', 'reconstruct']):
            categories['Reconstruction'].append(op)
            categorized = True
        elif any(kw in name or kw in label for kw in ['align', 'register', 'shift', 'rotation']):
            categories['Alignment'].append(op)
            categorized = True
        elif any(kw in name or kw in label for kw in ['filter', 'denoise', 'smooth', 'median', 'gaussian', 'noise']):
            categories['Filtering'].append(op)
            categorized = True
        elif any(kw in name or kw in label for kw in ['segment', 'threshold', 'label']):
            categories['Segmentation'].append(op)
            categorized = True
        elif any(kw in name or kw in label for kw in ['crop', 'pad', 'resample', 'bin', 'rotate', 'swap', 'transpose']):
            categories['Transformation'].append(op)
            categorized = True
        elif any(kw in name or kw in label for kw in ['analyze', 'measure', 'calculate']):
            categories['Analysis'].append(op)
            categorized = True
        
        if not categorized:
            categories['Utilities'].append(op)
    
    # Remove empty categories
    return {k: v for k, v in categories.items() if v}


def generate_all_docs(operators: List[Dict[str, Any]], output_dir: str):
    """Generate all documentation files."""
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    # Categorize operators
    categorized = categorize_operators(operators)
    
    # Generate index file
    index_lines = []
    index_lines.append("# Operators Reference")
    index_lines.append("")
    index_lines.append("This section documents all available operators in tomviz. Operators are Python scripts that process tomographic data through various transformations, reconstructions, and analyses.")
    index_lines.append("")
    index_lines.append(f"**Total operators:** {len(operators)}")
    index_lines.append("")
    
    # Summary by category
    index_lines.append("## Categories")
    index_lines.append("")
    for category, ops in sorted(categorized.items()):
        index_lines.append(f"- **{category}**: {len(ops)} operator(s)")
    index_lines.append("")
    
    index_lines.append("```{toctree}")
    index_lines.append("---")
    index_lines.append("maxdepth: 2")
    index_lines.append("---")
    
    for category in sorted(categorized.keys()):
        category_slug = category.lower().replace(' ', '_')
        index_lines.append(category_slug)
    
    index_lines.append("```")
    
    with open(output_path / 'index.md', 'w') as f:
        f.write('\n'.join(index_lines))
    print(f"Created: {output_path / 'index.md'}")
    
    # Generate category files
    for category, ops in categorized.items():
        category_slug = category.lower().replace(' ', '_')
        category_lines = []
        
        category_lines.append(f"# {category}")
        category_lines.append("")
        category_lines.append(f"This category contains {len(ops)} operator(s).")
        category_lines.append("")
        
        # Sort by label
        ops.sort(key=lambda x: x.get('label', x.get('name', '')))
        
        for op in ops:
            category_lines.append(generate_operator_markdown(op))
        
        with open(output_path / f'{category_slug}.md', 'w') as f:
            f.write('\n'.join(category_lines))
        print(f"Created: {output_path / category_slug}.md")


def update_main_index(docs_dir: str):
    """Update the main docs/index.md to include operators."""
    index_path = Path(docs_dir) / 'index.md'
    
    if not index_path.exists():
        print(f"Warning: {index_path} not found, skipping update")
        return
    
    with open(index_path, 'r') as f:
        content = f.read()
    
    # Check if operators are already referenced
    if 'operators/index' in content:
        print("Operators already referenced in main index")
        return
    
    # Add operators to toctree
    toctree_marker = "api/modules"
    if toctree_marker in content:
        content = content.replace(toctree_marker, f"{toctree_marker}\noperators/index")
        
        with open(index_path, 'w') as f:
            f.write(content)
        print(f"Updated: {index_path}")
    else:
        print("Warning: Could not find toctree in main index to update")


def main():
    """Main function."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Generate Sphinx documentation for tomviz operators'
    )
    parser.add_argument(
        '--local',
        type=str,
        help='Path to local directory containing JSON files'
    )
    parser.add_argument(
        '--github',
        action='store_true',
        help='Fetch JSON files from GitHub'
    )
    parser.add_argument(
        '--user',
        type=str,
        default='psavery',
        help='GitHub username (default: psavery)'
    )
    parser.add_argument(
        '--repo',
        type=str,
        default='tomviz',
        help='GitHub repository (default: tomviz)'
    )
    parser.add_argument(
        '--branch',
        type=str,
        default='ptycho-workflow',
        help='GitHub branch (default: ptycho-workflow)'
    )
    parser.add_argument(
        '--path',
        type=str,
        default='tomviz/python',
        help='Path in repository (default: tomviz/python)'
    )
    parser.add_argument(
        '--output',
        type=str,
        default='docs/operators',
        help='Output directory (default: docs/operators)'
    )
    
    args = parser.parse_args()
    
    operators = []
    
    if args.local:
        print(f"Loading JSON files from local directory: {args.local}")
        operators = load_local_json_files(args.local)
    elif args.github:
        print(f"Fetching JSON files from GitHub: {args.user}/{args.repo}/{args.branch}/{args.path}")
        operators = fetch_github_json(args.user, args.repo, args.branch, args.path)
    else:
        print("Error: Specify either --local <directory> or --github")
        parser.print_help()
        sys.exit(1)
    
    if not operators:
        print("No operators found!")
        sys.exit(1)
    
    print(f"\nFound {len(operators)} operator(s)")
    print("Generating documentation...")
    
    generate_all_docs(operators, args.output)
    update_main_index('docs')
    
    print(f"\n✓ Documentation generated successfully in {args.output}/")
    print("\nRebuild your Sphinx docs with:")
    print("  cd docs && make clean && make html")


if __name__ == '__main__':
    main()
