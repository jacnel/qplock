from tkinter import Y
from turtle import pd
from absl import flags
from absl import app
import os
import google.protobuf.text_format as text_format
import google.protobuf.descriptor as descriptor
import benchmarks.access_study.experiment_pb2 as experiment
import pandas
import matplotlib.pyplot as plt
import seaborn

flags.DEFINE_string("results_dir", None,
                    "The root directory of results to plot.")
flags.DEFINE_string("experiment", None,
                    "The experiment configuration used to generate the results")
flags.DEFINE_string(
    "datafile", None, "A path to a .csv file containing data to plot")

FLAGS = flags.FLAGS

def getData(proto, path=''):
    column_names = []
    values = []
    for field in proto.DESCRIPTOR.fields:
        if (field.label == descriptor.FieldDescriptor.LABEL_REPEATED and len(getattr(proto, field.name)) == 0) or (not field.label == descriptor.FieldDescriptor.LABEL_REPEATED and not proto.HasField(field.name)):
            continue
        if field.type is descriptor.FieldDescriptor.TYPE_MESSAGE:
            cols, vals = getData(
                getattr(proto, field.name),
                path + ('.' if len(path) != 0 else '') + field.name)
            column_names.extend(cols)
            values.extend(vals)
        else:
            values.append(getattr(proto, field.name))
            column_names.append(path + ('.' if len(path)
                                != 0 else '') + field.name)
    return column_names, values


def plot_throughput(data):
    data = data[data['experiment_params.name'].str.count('.*_c.*') == 0]

    x_ = 'experiment_params.cluster_size'
    y_ = 'driver.qps.summary.mean'
    columns = [x_, y_]
    data = data[columns]
    data[y_] *= 2
    data['type'] = 'client'

    # Calculate totals grouped by the cluster size
    totals = data.groupby(x_, as_index=False).sum()
    totals['type'] = 'total'
    print(totals)
    data = pandas.concat([data, totals])
    print(data)

    client_data = data[data['type'] == 'client']
    total_data = data[data['type'] == 'total']

    seaborn.set_theme(style='ticks')
    palette = seaborn.color_palette('rocket_r', n_colors=2)
    markersize = 10
    clients = seaborn.relplot(
        data=client_data,
        x=x_,
        y=y_,
        hue='type',
        kind='line',
        style='type',
        markers=True,
        markersize=markersize,
        ci='sd',
        palette=palette[0:1],
        height=3,
        aspect=1.75,
    )
    clients.ax.ticklabel_format(axis='y', scilimits=[-5,3])
    xticks = [int(x) for x in data[x_]]
    font = {'fontsize': 18}
    clients.ax.set_ylim(ymin=0, ymax=2e5)
    clients.ax.yaxis.set_major_locator(plt.MaxNLocator(5))
    clients.ax.set_xticks(range(xticks[0], xticks[-1], 20))
    clients.ax.tick_params(labelsize=14)
    clients.ax.set_ylabel("Throughput (ops/s)", font)
    clients.ax.set_xlabel("Num. Clients", font)

    totals = seaborn.lineplot(
        data=total_data,
        x=x_,
        y=y_,
        ax=clients.ax.twinx(),
        hue='type',
        style='type',
        markers=True,
        markersize=markersize,
        palette=palette[1:],
    )
    totals.tick_params(labelsize=14)
    totals.set_ylim(ymin=0, ymax=3e6)
    totals.set_ylabel("")

    h1, l1 = totals.get_legend_handles_labels()
    h2, l2 = clients.ax.get_legend_handles_labels()
    labels_handles = dict(zip(l1, h1))
    labels_handles.update(dict(zip(l2, h2)))
    totals.legend().remove()
    clients.legend.remove()
    clients.add_legend(labels_handles, title='', bbox_to_anchor=(.75, 0.5))

    clients.savefig('fig.png', dpi=300)


def plot_latency(data):
    columns = ['experiment_params.name', 'experiment_params.cluster_size',
               'driver.qps.summary.mean', 'driver.latency.summary.mean',
               'driver.latency.summary.p99', 'driver.latency.summary.p999']
    data = data[columns]
    data = data[data['experiment_params.name'].str.count('.*_c.*') == 0]


def main(argv):
    if FLAGS.datafile is None:
        result_files = os.walk(FLAGS.results_dir)
        result_protos = []
        for root, _, files in result_files:
            for name in files:
                with open(os.path.join(root, name)) as result_file:
                    result = text_format.Parse(
                        result_file.read(), experiment.ResultProto())
                    result_protos.append(result)

        cols = None
        vals = []
        for proto in result_protos:
            c, v = getData(proto)
            if cols is None:
                cols = c
            vals.append(v)
        print(cols)
        data = pandas.DataFrame(vals, columns=cols)
        data.to_csv('data.csv', index_label='row')
    else:
        data = pandas.read_csv(FLAGS.datafile)

    plot_throughput(data)
    plot_latency(data)


if __name__ == '__main__':
    app.run(main)
