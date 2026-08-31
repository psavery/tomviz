# Single source of truth for the tomviz-pipeline version this tomviz
# depends on.
#
# Read by setup.py (install_requires), by the CMake build at configure
# time (tomvizConfig.h -> TOMVIZ_PIPELINE_MIN_VERSION, for the external
# Python environment check), and runnable as a script to print the pip
# requirement for build instructions:
#
#     pip install --no-deps -U "$(python tomviz/_pipeline_requirement.py)"
#
# An external environment is compatible when its tomviz-pipeline is
# >= this version and < the next minor release, e.g. 3.1.3 accepts
# 3.1.x (x >= 3) and rejects 3.2.0.
#
# Keep this file free of imports: CMake parses it with a regular
# expression and setup.py executes it standalone.

TOMVIZ_PIPELINE_MIN_VERSION = '3.1.3'


def tomviz_pipeline_requirement():
    """``tomviz-pipeline>=X.Y.Z,<X.(Y+1)``: the range compatible with this
    tomviz."""
    version = TOMVIZ_PIPELINE_MIN_VERSION
    major, minor = (int(part) for part in version.split('.')[:2])
    return f'tomviz-pipeline>={version},<{major}.{minor + 1}'


if __name__ == '__main__':
    print(tomviz_pipeline_requirement())
