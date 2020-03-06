#!/bin/bash

if [[ $AGENT_OS == 'Windows_NT' ]]; then
  # Windows just has it labelled "python"
  PYTHON_EXE=python
else
  # On mac, "python" is python2
  PYTHON_EXE=python3
fi

versions_file=_versions.txt

paraview_sha1=$(git ls-remote https://github.com/openchemistry/paraview | head -1 | cut -f 1)

# Add more versions here if paraview needs to be re-built when
# these versions change.
echo $paraview_sha1 >> $versions_file
$PYTHON_EXE --version >> $versions_file
qmake --version >> $versions_file

deps_md5sum=$(md5sum $versions_file | cut -d ' ' -f1)
rm $versions_file

echo "##vso[task.setvariable variable=deps_md5sum]$deps_md5sum"
