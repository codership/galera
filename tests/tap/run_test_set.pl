#!/usr/bin/perl -w
#
# Initially shamelessly stolen from 
# http://testanything.org/wiki/index.php/TAP::Parser_Cookbook
#
# Name:          run_test_set.pl
# Purpose:       A test scheduler and runner
#
# Usage:
#
# run_test_set.pl [-q|-v] [<test.conf>]* ["<test script> [test args]"]* 
# 
# Options:
# -q Quiet mode (default)
# -v Verbose mode, prints report output also to STDOUT
#

use strict;
use warnings;

use POSIX qw/strftime/;
use TAP::Parser qw/all/;
use TAP::Parser::Aggregator qw/all/;

sub usage
{
    printf STDERR "Usage: $0 <test.conf>\n";
}


my $out_file;
my $quiet = 1;

sub printlog
{
    if ($quiet == 0)
    {
        print @_;
    }
    printf $out_file @_;
}



sub read_tests
{
    my @res = ();
    my $file = shift;
    open IN, "<", $file or die "Could not open file " . $file . ": " . $! . "\n";
    while (<IN>)
    {
        my $line;
        chomp($line = $_);
        unless ($line =~ /^#/)
        {
            push @res, $line;
        }
    }
    
    close IN;
    return @res;
}


# Find out if script is run from correct working directory
my $cwd;
chomp($cwd = `pwd`);
my $self = $cwd . "/scripts/run_test_set.pl";
die "run_test_set.pl must be run from test root" 
    unless system(("test", "-f", $self)) == 0;

# Export some helpful environment variables

# Count number of nodes for test scripts
my @nvec = split(' ', $ENV{CLUSTER_NODES});
$ENV{CLUSTER_N_NODES} = scalar(@nvec);

# Report directory and test result log definitions
# Report dir is r/yymmdd.i where yy is year, mm month, dd day and
# i running index, starting from 0. 
my $fmt_str = "%y%m%d";
my $i = 0;
my $report_dir = $cwd . "/r/" . strftime($fmt_str, localtime);

while (-d $report_dir . "." . $i)
{
    $i++;
}

$report_dir = $report_dir . "." . $i;

mkdir($report_dir, 0777) or 
    die "Could not create report dir " . $report_dir . ": " . $! . "\n";

my $result_log = $report_dir . "/result.log";

# Open result log
open $out_file, ">>", $result_log or die "Cannot open outfile. $!\n";


printlog sprintf("\n---\nReport %s\n---\n", 
                 strftime("%y%m%d-%H:%M", localtime));


# Scan given arguments for test definitions 

my @args = @ARGV;

while ($args[0] =~ /^-(\w)/)
{
    my $opt = $1;
#    print $opt . "\n";
    if ($opt eq "q")
    {
        $quiet = 1;
    }
    elsif ($opt eq "v")
    {
        $quiet = 0;
    }
    else
    {
        usage;
        die $0 . ": Unknown option: -" . $opt . "\n";
    }
    
    shift @args;
}

my @tests = ();

if (scalar(@args) == 0)
{
    printlog "No tests to be run\n";
    exit 0;
}

foreach my $arg (@args)
{
    if ($arg =~ /.conf/)
    {
        push @tests, read_tests($arg);
    }
    else
    {
        push(@tests, $arg);
    }
}


foreach my $test (@tests) 
{
    printlog "\n---\nRunning test: " . $test . "\n---\n";
    my @line = split(' ', $test);
    my $file = $line[0];
    shift(@line);
    $ENV{TEST_REPORT_DIR} = $report_dir;
    my $parser = TAP::Parser->new( { source => $file,
                                     test_args => \@line } );
    while ( my $result = $parser->next ) 
    {
        printlog $result->as_string . "\n";
    }
    my $aggregate = TAP::Parser::Aggregator->new;
    $aggregate->add( 'testcases', $parser );
    printlog sprintf("\nPassed: %s\nFailed: %s\n", 
                     scalar $aggregate->passed, 
                     scalar $aggregate->failed);
}





