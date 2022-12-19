#!/bin/bash
set -e
set -u

ctest2junit_xsl=$(readlink -f `dirname ${BASH_SOURCE[0]}`)/ctest_to_junit.xsl
tests_dir=$1

if [ ! -d $tests_dir ];
then
  echo "ERROR! $tests_dir is not directory!"
  exit 1
fi

tag=$(head -n 1 $tests_dir/Testing/TAG)
xsltproc --output build/results/ctest/results.xml $ctest2junit_xsl $tests_dir/Testing/$tag/Test.xml

echo "Test report converted successfully"

