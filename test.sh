#!/bin/bash

workspace=/home/amanda/qplock_rome/qplock/qplock
nodefile=~/qplock_rome/qplock/qplock/benchmark/nodefiles/c6525-25g.csv

#** FUNCTION DEFINITIONS **#

sync_nodes() {
  tmp=$(pwd)
  cd ../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/qplock_rome --local_root=/home/amanda/qplock_rome --sync
  cd ${tmp}
  echo "Sync Complete\n"
}

clean() {
  tmp=$(pwd)
  cd ../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321 --local_root=/home/amanda --cmd="cd qplock_rome/qplock && ~/go/bin/bazelisk clean"
  cd ${tmp}
  echo "Clean Complete\n"
}

build_lock() {
  tmp=$(pwd)
  cd ../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321 --local_root=/home/amanda --cmd="cd qplock_rome/qplock && ~/go/bin/bazelisk build -c opt --lock_type=$1 //qplock/benchmark/baseline:main"
  cd ${tmp}
  echo "Build Complete\n"
}

#** START OF SCRIPT **#

# echo "Cleaning..."
# clean

# echo "Pushing local repo to remote nodes..."
# sync_nodes

# #  LOCAL WORKLOAD PERFORMANCE
echo "Running Experiment #1: Spin Lock vs MCS vs A-Lock, 1 lock on 1 server"
lock='alock'
log_level='debug'
echo "Building ${lock}..."
build_lock ${lock}

echo "Running..."
save_dir="exp1"
for num_clients in 10
do
  bazel run //qplock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=500 --runtime=3 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock} --gdb=True
done
bazel run //qplock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}
