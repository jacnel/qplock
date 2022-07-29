#!/usr/bin/env python
import subprocess
import json
from multiprocessing import Process
from os import abort
import os
import sys
from absl import app
from absl import flags
import configparser
import benchmarks.access_study.experiment_pb2 as experiment
import google.protobuf.text_format as text_format

# `launch.py` is a helper script to run experiments remotely. It takes the same input paramters as the underlying script to execute along with additional parameters

FLAGS = flags.FLAGS


# Data collection configuration
flags.DEFINE_bool(
    "get_data", False,
    "Whether or not to print commands to retrieve data from client nodes")
flags.DEFINE_bool(
    "generate_experiments", False,
    "Whether to generate a new experiment file based on a base configuraion.",
    short_name='g')
flags.DEFINE_string(
    "nodefile", None,
    "If given then a new set of experiments will be generated using the given nodefile",
    short_name='n')
flags.DEFINE_string(
    "configfile", None,
    "The name of the configuration file to use when generating or running experiments.",
    short_name='c', required=True)
flags.DEFINE_string(
    "experiment_name", None,
    "The name of the experiment to run, or the output file when generating a new experiment",
    short_name='e')

flags.DEFINE_string("bazel_run", "run -c opt",
                    "The run command to pass to Bazel")

# Experiment configuration
flags.DEFINE_integer(
    "max_clients", 11,
    "The maximum number of clients to run on each node.")


ID = 0
PRIV_NAME = 1
PUBL_NAME = 2
TYPE = 3
ROLE = 4

# Parse the experiment file and make it globally accessible
config = configparser.ConfigParser(
    interpolation=configparser.ExtendedInterpolation())
launch = None
workload = None


class Node:
    def __init__(self, id, priv_name, publ_name, type, role):
        self.id = id
        self.priv_name = priv_name
        self.publ_name = publ_name
        self.type = type
        self.role = role


__utah__ = ["xl170", "c6525-100g"]
__emulab__ = ["r320"]


def build_hostname(node):
    if node.type in __utah__:
        return node.publ_name + ".utah.cloudlab.us"
    elif node.type in __emulab__:
        return node.publ_name + ".apt.emulab.net"
    else:
        abort()


def build_ssh_command(node):
    return 'ssh ' + launch["user"] + '@' + build_hostname(node)


def build_common_command():
    return launch["cmd_prefix"] + (' ' if len(launch["cmd_prefix"]) > 0 else '') + launch["bazel_bin"] + ' ' + FLAGS.bazel_run + ' //src/benchmark/access_study:main -- '


def make_one_line(proto):
    return ' '.join(line for line in str(proto).split('\n'))


SINGLE_QUOTE = "'\"'\"'"


def quote(line):
    return SINGLE_QUOTE + line + SINGLE_QUOTE


def build_common_flags(experiment, experiment_params, access_params):
    nodes = ':'.join(json.loads(config[experiment]['nodes']))
    return '--nodes ' + quote(nodes) + ' --experiment_params ' + quote(
        make_one_line(experiment_params)) + ' --access_params ' + quote(
        make_one_line(access_params))


def build_server_command(experiment, client_ids):
    experiment_params = build_experiment_params("kServer", client_ids)
    experiment_params.name = experiment
    access_params = build_access_params()
    return build_common_command() + build_common_flags(experiment,
                                                       experiment_params, access_params)


def build_experiment_params(mode, client_ids):
    proto = text_format.Parse(
        workload["experiment_params"], experiment.ExperimentParams())
    proto.mode = mode
    proto.client_ids.extend(int(id) for id in client_ids)
    return proto


def build_access_params():
    proto = text_format.Parse(
        workload["access_params"], experiment.AccessParams())
    return proto


def build_step_params():
    proto = text_format.Parse(
        workload["step_params"], experiment.StepParams())
    return proto


def build_client_command(experiment, nodes):
    experiment_params = build_experiment_params(
        "kClient", [n.id for n in nodes])
    experiment_params.name = experiment
    access_params = build_access_params()
    return build_common_command() + build_common_flags(experiment,
                                                       experiment_params, access_params) + ' --step_params ' + quote(
        make_one_line(str(build_step_params())))


def build_get_data_command(node):
    return 'rsync -rq ' + launch["user"] + '@' + build_hostname(node) + ':' + os.path.join(
        launch["save_root"], config["setup"]["save_dir"]) + '/* ' + os.path.join(launch["dest_root"], config["setup"]["save_dir"],
                                                                                 node.priv_name) + '/'


def generate_single_node_scaling(lines):
    global config
    # Generate clients on a single nodes
    experiments = []
    for i in range(1, FLAGS.max_clients + 1):
        section = 'n1_c' + str(i)
        nodes = lines[0:1]
        for j in range(0, i):
            nodes.extend([str(j + 1) + ',' + ','.join(lines[1].split(',')[1:])])
        config.add_section(section)
        config[section]['nodes'] = '[' + ',\n'.join('"' + n + '"'
                                                    for n in nodes) + ']'
        experiments.append(section)
    return experiments


def generate_ten_node_scaling(lines):
    global config

    experiments = []
    for i in range(1, len(lines), 2):
        # Generate odd numbers of clients, one for each node
        section = 'n' + str(i)
        config.add_section(section)
        config[section]['nodes'] = '[' + ',\n'.join('"' + l + '"'
                                                    for l in lines[0: i + 1]) + ']'
        experiments.append(section)

    nclients = len(lines[1:])
    for i in range(0, FLAGS.max_clients):
        # Generate some number of clients per node
        nodes = lines.copy()
        for j in range(1, i + 1):  # Clients are 1-indexed
            nodes.extend(
                [str(id + (j * nclients)) + ',' + ','.join(c.split(',')[1:])
                 for id, c in zip(range(1, nclients + 1),
                                  lines[1:])])
        section = 'n' + str(nclients * (i + 1))
        config.add_section(section)
        config[section]['nodes'] = '[' + ',\n'.join(
            '"' + n + '"' for n in nodes) + ']'
        experiments.append(section)
    return experiments


def __run__(cmd):
    print("Running command: {}".format(cmd))
    subprocess.run(cmd, shell=True, check=True)


def main(args):
    global launch
    global workload

    config.read(FLAGS.configfile)

    launch = config["launch"]
    workload = config["workload"]

    if FLAGS.generate_experiments:
        if FLAGS.nodefile is not None:
            with open(FLAGS.nodefile) as f:
                lines = f.read().split('\n')
                assert len(lines) >= 11, "Nodefile needs at least 11 nodes"
                for i in range(0, len(lines)):
                    lines[i] = str(
                        i) + ',' + lines[i] + ',' + ('kServer' if i == 0 else 'kClient')
                experiments = []
                experiments.extend(generate_single_node_scaling(lines))
                experiments.extend(generate_ten_node_scaling(lines))
                workload['experiments'] = '[' + ','.join('"' + e + '"'
                                                         for e in experiments) + ']'

            assert FLAGS.experiment_name is not None
            path = os.path.join('experiments', FLAGS.experiment_name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w') as experimentfile:
                config.write(experimentfile)
        else:
            print("Please provide a nodefile when generating experiments")
            exit(1)

    experiment_list = {}
    for experiment in json.loads(workload['experiments']):
        nodelist = []
        for node in json.loads(config[experiment]['nodes']):
            parts = node.split(',')
            node = Node(
                parts[ID],
                parts[PRIV_NAME],
                parts[PUBL_NAME],
                parts[TYPE],
                parts[ROLE])
            nodelist.append(node)

        # Generate a map of clients and their associated ids
        nodes = {}
        client_ids = []
        for n in nodelist:
            # Save client ids for the server command
            if n.role == "kClient":
                client_ids.append(n.id)

            if (n.priv_name, n.role) not in nodes:
                nodes[(n.priv_name, n.role)] = [n]
            else:
                nodes[(n.priv_name, n.role)].append(n)

        left = len(nodes)
        if not FLAGS.get_data:
            experiment_list[experiment] = []
            for n in nodes:
                if n[1] == "kServer":
                    experiment_list[experiment].append(
                        build_ssh_command(nodes[n][0]) + ' \'' +
                        build_server_command(experiment, client_ids) + '\'')
                else:
                    experiment_list[experiment].append(
                        build_ssh_command(nodes[n][0]) + ' \'' +
                        build_client_command(experiment, nodes[n]) + '\'')
        else:
            experiment_list['get_data'] = []
            for client in [n for n in nodelist if n.role == 'kClient']:
                # print(build_get_data_command(client))
                experiment_list['get_data'].append(build_get_data_command(client))

    for e in experiment_list:
        processes = []
        print("Running experiment: {}".format(e))
        for c in experiment_list[e]:
            processes.append(Process(target=__run__, args=(c,)))
            processes[-1].start()

        for p in processes:
            p.join()


if __name__ == "__main__":
    app.run(main)
