#!/usr/bin/env python
import configparser
import itertools
import json
import os
import subprocess
import sys
from math import log
from multiprocessing import Process
from os import abort
from time import sleep

import qplock.benchmark.baseline.experiment_pb2 as experiment_pb2
import pandas
from absl import app, flags
from alive_progress import alive_bar
import qplock.benchmark.baseline.plot as plot
from rome.rome.util.debugpy_util import debugpy_hook

# `launch.py` is a helper script to run experiments remotely. It takes the same input paramters as the underlying script to execute along with additional parameters

FLAGS = flags.FLAGS


# Data collection configuration
flags.DEFINE_bool(
    'get_data', False,
    'Whether or not to print commands to retrieve data from client nodes')
flags.DEFINE_string(
    'save_root',
    'qplock_rome/qplock/',
    'Directory results are saved under when running')
flags.DEFINE_string('nodefile', None, 'Path to nodefile',
                    short_name='n', required=True)
flags.DEFINE_string('remote_save_dir', 'results', 'Remote save directory')
flags.DEFINE_string('local_save_dir', 'results', 'Local save directory')
flags.DEFINE_bool(
    'plot', False,
    'Generate plots (should ensure `--get_data` is called first)')
flags.DEFINE_string(
    'expfile', 'experiments.csv',
    'Name of file containing experiment configurations to run (if none exists then one is generated)'
)

# Workload definition
flags.DEFINE_multi_string('lock_type', 'spin', 'Locks to test in experiment')
flags.DEFINE_multi_integer('think_ns', 500, 'Think times in nanoseconds')

flags.DEFINE_integer('min_key', 0, 'Minimum key')
flags.DEFINE_integer('max_key', int(1e6), 'Maximum key')
flags.DEFINE_float('theta', 0.99, 'Theta in Zipfian distribution')
flags.DEFINE_bool('stabilize', False, 'Perform deletions to stabilize size')
flags.DEFINE_integer('runtime', 10, 'Number of seconds to run experiment')

flags.DEFINE_string('ssh_user', 'adb321', '')
flags.DEFINE_string('ssh_keyfile', '~/.ssh/id_ed25519', '')
flags.DEFINE_string('bazel_flags', '-c opt',
                    'The run command to pass to Bazel')
flags.DEFINE_string('bazel_bin', '~/go/bin/bazelisk',
                    'Location of bazel binary')
flags.DEFINE_string('bazel_prefix', 'cd qplock_rome/qplock/ && ',
                    'Command to run before bazel')
flags.DEFINE_bool('gdb', False, 'Run in gdb')

# Experiment configuration
flags.DEFINE_integer('server_memory', int(2e9),
                     'Number of bytes to alloc on server')
flags.DEFINE_multi_integer(
    'servers', 1, 'Number of servers in cluster', short_name='s')
flags.DEFINE_multi_integer(
    'workers', 0, 'Number of workers to run locally on server',
    short_name='w')
flags.DEFINE_multi_integer(
    'clients', 10, 'Number of clients in cluster', short_name='c')

flags.DEFINE_integer(
    'restrict_servers', 0,
    'Maximum number of nodes to use for servers (0 indicates that as many as possible should be used)')
flags.DEFINE_integer(
    'restrict_clients', 0,
    'Maximum number of nodes to use for clients (0 indicates that as many as possible should be used)')

flags.DEFINE_integer(
    'port', 18018, 'Port to listen for incoming connections on')
flags.DEFINE_string('log_dest', '/home/amanda/logs/alock',
                    'Name of local log directory for ssh commands')
flags.DEFINE_boolean(
    'dry_run', False,
    'Prints out the commands to run without actually executing them')
flags.DEFINE_boolean(
    'debug', False, 'Whether to launch a debugpy server to debug this program')
flags.DEFINE_string('log_level', 'info', 'Rome logging level to launch client & servers with')



# Parse the experiment file and make it globally accessible
config = configparser.ConfigParser(
    interpolation=configparser.ExtendedInterpolation())
launch = None
workload = None

__lehigh__ = ['luigi']
__utah__ = ['xl170', 'c6525-100g', 'c6525-25g', 'd6515']
__clemson__ = ['r6525']
__emulab__ = ['r320']


def get_domain(node_type):
    if node_type in __utah__:
        return 'utah.cloudlab.us'
    elif node_type in __clemson__:
        return 'clemson.cloudlab.us'
    elif node_type in __emulab__:
        return 'apt.emulab.net'
    elif node_type in __lehigh__:
        return 'cse.lehigh.edu'
    else:
        abort()


def partition_nodefile(path, n_server_nodes):
    # Put a server on own machines
    # print(path)
    # print(os.getcwd())
    all_csv = []
    assert(os.path.exists(path))
    with open(path, 'r') as __file:
        all_csv = [line for line in __file]
    assert n_server_nodes < len(all_csv), "No machines left"
    if n_server_nodes == -1:  # Conveniece for getting data
        return all_csv, None
    servers_csv = all_csv[0:n_server_nodes]
    clients_csv = all_csv[n_server_nodes:]
    if FLAGS.restrict_servers > 0:
        servers_csv = servers_csv[0:FLAGS.restrict_servers]
    if FLAGS.restrict_clients > 0:
        clients_csv = clients_csv[0:FLAGS.restrict_clients]
    return servers_csv, clients_csv


def parse_clients(csv, nid, num_clients):
    nodes = []
    name, public_hostname, node_type = csv[0].strip().split(',')
    nodes.append((name, public_hostname))
    for line in csv[1:]:
        name, public_hostname, _ = line.strip().split(',')
        nodes.append((name, public_hostname))

    proto = experiment_pb2.ClusterProto()
    proto.node_type = node_type
    proto.domain = get_domain(node_type)
    clients = {}
    for r in range(0, num_clients):
        i = nid % len(nodes)
        n = nodes[i]
        c = experiment_pb2.NodeProto(
            nid=nid, private_hostname=n[0], public_hostname=n[1],
            port=FLAGS.port + nid)
        proto.clients.append(c)
        if clients.get(n[0]) is None:
            clients[n[0]] = []
        clients[n[0]].append(c)
        nid += 1
    return proto, clients


def parse_servers(csv, nid,  num_servers):
    nodes = []
    name, public_hostname, node_type = csv[0].strip().split(',')
    nodes.append((name, public_hostname))
    for line in csv[1:]:
        name, public_hostname, _ = line.strip().split(',')
        nodes.append((name, public_hostname))

    proto = experiment_pb2.ClusterProto()
    proto.node_type = node_type
    proto.domain = get_domain(node_type)
    servers = {}

    if num_servers == -1:
        num_servers = len(nodes)

    for _i in range(0, num_servers):
        i = nid % len(nodes)
        n = nodes[i]
        s = experiment_pb2.NodeProto(
            nid=nid, private_hostname=n[0],
            public_hostname=n[1],
            port=FLAGS.port + nid)
        proto.servers.append(s)
        if servers.get(n[0]) is None:
            servers[n[0]] = []
        servers[n[0]].append(s)
        nid += 1
    return proto, servers


def build_hostname(name, domain):
    return '.'.join([name, domain])


def build_ssh_command(name, domain):
    return ' '.join(
        ['ssh -i', FLAGS.ssh_keyfile, FLAGS.ssh_user + '@' +
         build_hostname(name, domain)])


def build_common_command(lock, params, cluster):
    cmd = FLAGS.bazel_prefix + (' ' if len(FLAGS.bazel_prefix) > 0 else '')
    cmd = ' '.join([cmd, FLAGS.bazel_bin])
    if not FLAGS.gdb:
        cmd = ' '.join([cmd, 'run'])
        cmd = ' '.join([cmd, FLAGS.bazel_flags])
        cmd = ' '.join([cmd, '--lock_type=' + lock])
        cmd = ' '.join([cmd, '--log_level=' + FLAGS.log_level])
        cmd = ' '.join([cmd, '//qplock/benchmark/baseline:main --'])
    else:
        cmd = ' '.join([cmd, 'build'])
        cmd = ' '.join([cmd, FLAGS.bazel_flags])
        cmd = ' '.join([cmd, '--lock_type=' + lock])
        cmd = ' '.join([cmd, '--log_level=' + FLAGS.log_level])
        cmd = ' '.join([cmd, '--copt=-g', '--strip=never',
                        '//qplock/benchmark/baseline:main'])
        cmd = ' '.join([cmd, '&& gdb -ex run -ex bt -ex q -ex y --args',
                        'bazel-bin/qplock/benchmark/baseline/main'])
    cmd = ' '.join([cmd, '--experiment_params', quote(make_one_line(params))])
    cmd = ' '.join([cmd, '--cluster', quote(make_one_line(cluster))])
    return cmd


def build_command(ssh_command, run_command):
    return ' '.join(
        [ssh_command, '\'' + run_command + '\''])


def make_one_line(proto):
    return ' '.join(line for line in str(proto).split('\n'))


SINGLE_QUOTE = "'\"'\"'"


def quote(line):
    return SINGLE_QUOTE + line + SINGLE_QUOTE


def build_save_dir(lock):
    return os.path.join(FLAGS.remote_save_dir, lock)


def build_remote_save_dir(lock):
    return os.path.join(
        FLAGS.save_root, FLAGS.remote_save_dir) + '/*'


def build_local_save_dir(lock, public_hostname):
    return os.path.join(FLAGS.local_save_dir, lock, public_hostname) + '/'


def fill_experiment_params_common(
        proto, experiment_name, lock, nservers, nclients, think):
    proto.name = experiment_name
    proto.num_servers = nservers
    proto.num_clients = nclients
    proto.cluster_size = nclients + nservers
    # proto.num_readonly = nreadonly
    proto.workload.runtime = FLAGS.runtime
    proto.workload.think_time_ns = think
    proto.save_dir = build_save_dir(lock)
    # proto.workload.min_key = FLAGS.min_key
    # proto.workload.max_key = FLAGS.max_key
    # proto.workload.theta = FLAGS.theta
    # proto.workload.stabilize = FLAGS.stabilize
    # proto.workload.min_rq_size = FLAGS.min_rq_size
    # proto.workload.max_rq_size = FLAGS.max_rq_size
    # proto.workload.worker_threads = nworkers
    # proto.workload.get_one_sided = FLAGS.remote_gets
    # proto.workload.scan_one_sided = FLAGS.remote_scans
    # proto.workload.read_only_one_sided = FLAGS.read_only_one_sided if FLAGS.datastore != 'masstree' else False
    return proto


def build_server_experiment_params(server_id):
    proto = experiment_pb2.ExperimentParams()
    proto.mode = experiment_pb2.Mode.SERVER
    proto.server_id = server_id
    return proto


def build_client_experiment_params(clients):
    proto = experiment_pb2.ExperimentParams()
    proto.mode = experiment_pb2.Mode.CLIENT
    if clients:
        proto.client_ids.extend(c.nid for c in clients)
    return proto


def build_get_data_command(lock, node, cluster):
    src = build_remote_save_dir(lock)
    dest = build_local_save_dir(lock, node.public_hostname)
    os.makedirs(dest, exist_ok=True)
    return 'rsync -aq ' + FLAGS.ssh_user + '@' + build_hostname(
        node.public_hostname, cluster.domain) + ':' + ' '.join(
        [src, dest])


def build_logfile_path(lock, experiment_name, mode, node):
    return os.path.join(
        FLAGS.log_dest, lock, experiment_name, mode, node.public_hostname +
        '_' + str(node.nid) + '.log')


def __run__(cmd, outfile, retries):
    failures = 0
    with open(outfile, 'w+') as f:
        # A little hacky, but we have a problem with bots hogging the SSH connections and causing the server to refuse the request.
        while (failures <= retries):
            try:
                subprocess.run(cmd, shell=True, check=True,
                               stderr=f, stdout=f)
                return
            except subprocess.CalledProcessError:
                failures += 1
        print(
            f'\t> Command failed. Check logs: {outfile} (trial {failures}/10)')


def execute(experiment_name, commands):
    processes = []
    for cmd in commands:
        if not FLAGS.dry_run:
            os.makedirs(
                os.path.dirname(cmd[1]),
                exist_ok=True)
            processes.append(
                Process(target=__run__, args=cmd))
            processes[-1].start()
        else:
            print(cmd[0])

    if not FLAGS.dry_run:
        for p in processes:
            p.join()


def main(args):
    debugpy_hook() 
    if FLAGS.get_data:
        nodes_csv, _ = partition_nodefile(FLAGS.nodefile, -1)
        cluster_proto, nodes = parse_servers(nodes_csv, 0, len(nodes_csv))
        commands = []
        with alive_bar(len(nodes.keys()), title="Getting data...") as bar:
            for _, nodes in nodes.items():
                n = nodes[0]
                # Run servers in separate processes
                execute('get_data',
                        [(build_get_data_command(
                            '', n, cluster_proto),
                            build_logfile_path('', '', 'get_data', n), 0)])
                bar()
    elif FLAGS.plot:
        datafile = FLAGS.datafile
        if datafile is None:
            datafile = '__data__.csv'
        if not os.path.exists(datafile):
            plot.generate_csv(FLAGS.local_save_dir, datafile)
        plot.plot(datafile, FLAGS.lock_type)
        if FLAGS.datafile is None:
            os.remove(datafile)
    else:
        columns = ['lock', 's',  'c', 't',  'done']
        experiments = {}
        if not FLAGS.dry_run and os.path.exists(FLAGS.expfile):
            print("EXP FILE EXISTS: ", os.path.abspath(FLAGS.expfile))
            experiments = pandas.read_csv(FLAGS.expfile, index_col='row')
        else:
            configurations = list(itertools.product(
                set(FLAGS.lock_type),
                set(FLAGS.servers),
                set(FLAGS.clients),
                set(FLAGS.think_ns),
                [False]))
            experiments = pandas.DataFrame(configurations, columns=columns)
            if not FLAGS.dry_run:
                experiments.to_csv(FLAGS.expfile, mode='w+', index_label='row')

        with alive_bar(int(experiments.shape[0]), title='Running experiments...') as bar:
            for index, row in experiments.iterrows():
                if row['done']:
                    bar()
                    continue

                lock = row['lock']
                s_count = row['s']
                c_count = row['c']
                think = row['t']

                servers_csv, clients_csv = partition_nodefile(
                    FLAGS.nodefile, s_count)
                if servers_csv is None:
                    continue
                cluster_proto = experiment_pb2.ClusterProto()
                temp, servers = parse_servers(servers_csv, 0, s_count)
                cluster_proto.MergeFrom(temp)
                temp, clients = parse_clients(
                    clients_csv, len(servers_csv), c_count)
                cluster_proto.MergeFrom(temp)

                commands = []
                experiment_name = 'l' + lock + '_s' + str(len(servers)) + '.' + str(
                    s_count) + '_c' + str(len(clients)) + '.' + str(c_count) + '_t' + str(think)
                bar.text = f'Lock type: {lock} | Current experiment: {experiment_name}'
                if not FLAGS.get_data:
                    for _, server_list in servers.items():
                        # Run servers in separate processes
                        for server in server_list:
                            ssh_command = build_ssh_command(
                                server.public_hostname, cluster_proto.domain)
                            params = build_server_experiment_params(
                                server.nid)
                            params = fill_experiment_params_common(
                                params, experiment_name, lock, s_count,
                                c_count, think)
                            run_command = build_common_command(
                                lock, params, cluster_proto)
                            commands.append((
                                build_command(
                                    ssh_command, run_command),
                                build_logfile_path(
                                    lock, experiment_name,
                                    'server', server), 100))
                    for n in set(clients.keys()):
                        client_list = clients.get(n)
                        node = client_list[0]
                        ssh_command = build_ssh_command(
                            node.public_hostname,
                            cluster_proto.domain)
                        params = build_client_experiment_params(
                            client_list)
                        params = fill_experiment_params_common(
                            params, experiment_name, lock, s_count,
                            c_count, think)
                        run_command = build_common_command(
                            lock, params, cluster_proto)
                        commands.append((
                            build_command(
                                ssh_command, run_command),
                            build_logfile_path(
                                lock, experiment_name,
                                'client',
                                node), 100))

                    # Execute the commands.
                    execute(experiment_name, commands)
                    experiments.at[index, 'done'] = True
                    experiments.to_csv(FLAGS.expfile, index_label='row')
                    bar()

        if not FLAGS.dry_run and os.path.exists(FLAGS.expfile):
            os.remove(FLAGS.expfile)


if __name__ == '__main__':
    app.run(main)
