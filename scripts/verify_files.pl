#!/usr/bin/perl

use strict;
use warnings;

# List of required files
my @required_files = (
  # Root files
  'build.sh',
  'CMakeLists.txt',

  # Utils
  'utils/CMakeLists.txt',

  # Core Library

  # Server App
  'app/server/CMakeLists.txt',

  # App Sources

  # Main
  'app/main.cc',
  'app/CMakeLists.txt',
);

my @missing_files;

foreach my $file (@required_files) {
  if (! -f $file) {
    push(@missing_files, $file);
  }
}

if (@missing_files) {
  print "ERROR: The following files are missing:\n";
  foreach my $missing (@missing_files) {
    print " - $missing\n";
  }

  exit 1;
}

print "All required files are present\n";
exit 0;

