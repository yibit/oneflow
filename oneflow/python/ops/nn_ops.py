from __future__ import absolute_import

import oneflow.python.framework.compile_context as compile_context
import oneflow.python.framework.remote_blob as remote_blob_util
import oneflow.python.framework.id_util as id_util
import oneflow.core.operator.op_conf_pb2 as op_conf_util
import oneflow.core.register.logical_blob_id_pb2 as logical_blob_id_util
import oneflow
from oneflow.python.oneflow_export import oneflow_export

import collections


@oneflow_export("nn.conv2d")
def conv2d(
    input,
    filters,
    strides,
    padding,
    data_format="NHWC",
    dilations=None,
    name=None,
):
    assert len(input.static_shape) == 4
    assert len(filters.static_shape) == 4

    if isinstance(strides, (list, tuple)):
        assert len(strides) == 2, ValueError(
            "strides length must be 2 when passed as a list."
        )
    elif isinstance(strides, int):
        strides = [strides, strides]
    else:
        raise ValueError("strides must be an int or a list.")

    if padding.upper() != "SAME" and padding.upper() != "VALID":
        raise ValueError('padding must be "SAME" or "VALID".')

    if data_format.upper() != "NCHW" and data_format.upper() != "NHWC":
        raise ValueError('data_format must be "NHWC" or "NCHW".')

    channel_pos = (
        "channels_first" if data_format.startswith("NC") else "channels_last"
    )

    if dilations is None:
        dilations = [1, 1]
    else:
        if isinstance(dilations, (list, tuple)):
            assert len(dilations) == 2, ValueError(
                "dilations length must be 2 when passed as a list."
            )
        elif isinstance(dilations, int):
            dilations = [dilations, dilations]
        else:
            raise ValueError("dilations must be an int or a list.")

    op_conf = op_conf_util.OperatorConf()
    setattr(op_conf, "name", name if name is not None else id_util.UniqueStr("Conv2d_"))
    setattr(op_conf.conv_2d_conf, "in", input.logical_blob_name)
    op_conf.conv_2d_conf.out = "out"
    op_conf.conv_2d_conf.weight = filters.logical_blob_name
    op_conf.conv_2d_conf.filters = filters.static_shape[0]
    op_conf.conv_2d_conf.padding = padding.lower()
    op_conf.conv_2d_conf.data_format = channel_pos
    if channel_pos == "channels_first":
        op_conf.conv_2d_conf.kernel_size.extend(filters.static_shape[2:4])
    elif channel_pos == "channels_last":
        op_conf.conv_2d_conf.kernel_size.extend(filters.static_shape[-3:-1])
    else:
        raise ValueError("invalid data_format")
    op_conf.conv_2d_conf.strides.extend(strides)
    op_conf.conv_2d_conf.dilation_rate.extend(dilations)
    op_conf.conv_2d_conf.use_bias = False

    compile_context.CurJobAddOp(op_conf)
    lbi = logical_blob_id_util.LogicalBlobId()
    lbi.op_name = op_conf.name
    lbi.blob_name = "out"
    return remote_blob_util.RemoteBlob(lbi)


@oneflow_export("nn.bias_add")
def bias_add(value, bias, data_format=None, name=None):
    if name is None:
        name = id_util.UniqueStr("BiasAdd_")

    if data_format is None:
        bias_add_axis = 1
    else:
        if data_format.startswith("NC"):
            bias_add_axis = 1
        elif data_format.startswith("N") and data_format.endswith("C"):
            bias_add_axis = len(value.static_shape) - 1
        else:
            raise ValueError(
                "data_format must be of the form `N...C` or `NC...`"
            )
    bias_extended_shape = [1] * len(value.shape)
    bias_extended_shape[bias_add_axis] = value.shape[bias_add_axis]
    assert bias_extended_shape[bias_add_axis] == bias.shape[0]
    bias = oneflow.reshape(bias, bias_extended_shape)

    return value + bias


@oneflow_export("nn.max_pool1d")
def max_pool1d(input, ksize, strides, padding, data_format="NWC", name=None):
    # TODO: fix cuDNN bugs in pooling_1d
    raise NotImplementedError


@oneflow_export("nn.avg_pool1d")
def avg_pool1d(input, ksize, strides, padding, data_format="NWC", name=None):
    # TODO: fix cuDNN bugs in pooling_1d
    raise NotImplementedError


@oneflow_export("nn.max_pool2d")
def max_pool2d(input, ksize, strides, padding, data_format="NHWC", name=None):
    op_conf = op_conf_util.OperatorConf()
    setattr(
        op_conf,
        "name",
        name if name is not None else id_util.UniqueStr("MaxPool2D_"),
    )
    setattr(op_conf.max_pooling_2d_conf, "in", input.logical_blob_name)
    setattr(op_conf.max_pooling_2d_conf, "out", "out")
    op_conf.max_pooling_2d_conf.pool_size[:] = _GetSequence(ksize, 2, "ksize")
    op_conf.max_pooling_2d_conf.strides[:] = _GetSequence(strides, 2, "strides")
    assert padding in ["VALID", "SAME"]
    setattr(op_conf.max_pooling_2d_conf, "padding", padding)
    assert data_format in ["NHWC", "NCHW", "NCHW_VECT_C"]
    setattr(
        op_conf.max_pooling_2d_conf,
        "data_format",
        "channels_last" if data_format == "NHWC" else "channels_first",
    )
    compile_context.CurJobAddOp(op_conf)
    out_lbi = logical_blob_id_util.LogicalBlobId()
    setattr(out_lbi, "op_name", op_conf.name)
    setattr(out_lbi, "blob_name", "out")
    return remote_blob_util.RemoteBlob(out_lbi)


@oneflow_export("nn.avg_pool2d")
def avg_pool2d(input, ksize, strides, padding, data_format="NHWC", name=None):
    op_conf = op_conf_util.OperatorConf()
    setattr(
        op_conf,
        "name",
        name if name is not None else id_util.UniqueStr("AveragePool2D_"),
    )
    setattr(op_conf.average_pooling_2d_conf, "in", input.logical_blob_name)
    setattr(op_conf.average_pooling_2d_conf, "out", "out")
    op_conf.average_pooling_2d_conf.pool_size[:] = _GetSequence(
        ksize, 2, "ksize"
    )
    op_conf.average_pooling_2d_conf.strides[:] = _GetSequence(
        strides, 2, "strides"
    )
    assert padding in ["VALID", "SAME"]
    setattr(op_conf.average_pooling_2d_conf, "padding", padding)
    assert data_format in ["NHWC", "NCHW", "NCHW_VECT_C"]
    setattr(
        op_conf.average_pooling_2d_conf,
        "data_format",
        "channels_last" if data_format == "NHWC" else "channels_first",
    )
    compile_context.CurJobAddOp(op_conf)
    out_lbi = logical_blob_id_util.LogicalBlobId()
    setattr(out_lbi, "op_name", op_conf.name)
    setattr(out_lbi, "blob_name", "out")
    return remote_blob_util.RemoteBlob(out_lbi)


@oneflow_export("nn.max_pool3d")
def max_pool3d(input, ksize, strides, padding, data_format="NDHWC", name=None):
    op_conf = op_conf_util.OperatorConf()
    setattr(
        op_conf,
        "name",
        name if name is not None else id_util.UniqueStr("MaxPool3D_"),
    )
    setattr(op_conf.max_pooling_3d_conf, "in", input.logical_blob_name)
    setattr(op_conf.max_pooling_3d_conf, "out", "out")
    op_conf.max_pooling_3d_conf.pool_size[:] = _GetSequence(ksize, 3, "ksize")
    op_conf.max_pooling_3d_conf.strides[:] = _GetSequence(strides, 3, "strides")
    assert padding in ["VALID", "SAME"]
    setattr(op_conf.max_pooling_3d_conf, "padding", padding)
    assert data_format in ["NDHWC", "NCDHW"]
    setattr(
        op_conf.max_pooling_3d_conf,
        "data_format",
        "channels_last" if data_format == "NDHWC" else "channels_first",
    )
    compile_context.CurJobAddOp(op_conf)
    out_lbi = logical_blob_id_util.LogicalBlobId()
    setattr(out_lbi, "op_name", op_conf.name)
    setattr(out_lbi, "blob_name", "out")
    return remote_blob_util.RemoteBlob(out_lbi)


@oneflow_export("nn.avg_pool3d")
def avg_pool3d(input, ksize, strides, padding, data_format="NDHWC", name=None):
    op_conf = op_conf_util.OperatorConf()
    setattr(
        op_conf,
        "name",
        name if name is not None else id_util.UniqueStr("AveragePool3D_"),
    )
    setattr(op_conf.average_pooling_3d_conf, "in", input.logical_blob_name)
    setattr(op_conf.average_pooling_3d_conf, "out", "out")
    op_conf.average_pooling_3d_conf.pool_size[:] = _GetSequence(
        ksize, 3, "ksize"
    )
    op_conf.average_pooling_3d_conf.strides[:] = _GetSequence(
        strides, 3, "strides"
    )
    assert padding in ["VALID", "SAME"]
    setattr(op_conf.average_pooling_3d_conf, "padding", padding)
    assert data_format in ["NDHWC", "NCDHW"]
    setattr(
        op_conf.average_pooling_3d_conf,
        "data_format",
        "channels_last" if data_format == "NDHWC" else "channels_first",
    )
    compile_context.CurJobAddOp(op_conf)
    out_lbi = logical_blob_id_util.LogicalBlobId()
    setattr(out_lbi, "op_name", op_conf.name)
    setattr(out_lbi, "blob_name", "out")
    return remote_blob_util.RemoteBlob(out_lbi)


@oneflow_export("nn.softmax")
def softmax(logits, axis=None, name=None):
    if axis is None:
        axis = -1
    assert type(axis) is int
    op_conf = op_conf_util.OperatorConf()
    setattr(
        op_conf,
        "name",
        name if name is not None else id_util.UniqueStr("Softmax_"),
    )
    setattr(op_conf.softmax_conf, "in", logits.logical_blob_name)
    op_conf.softmax_conf.axis = axis
    op_conf.softmax_conf.out = "out"
    compile_context.CurJobAddOp(op_conf)
    lbi = logical_blob_id_util.LogicalBlobId()
    lbi.op_name = op_conf.name
    lbi.blob_name = "out"
    return remote_blob_util.RemoteBlob(lbi)


@oneflow_export("nn.sparse_softmax_cross_entropy_with_logits")
def sparse_softmax_cross_entropy_with_logits(
    labels=None, logits=None, name=None
):
    assert labels is not None
    assert logits is not None
    op_conf = op_conf_util.OperatorConf()
    setattr(
        op_conf,
        "name",
        name if name is not None else id_util.UniqueStr("SparseCrossEntropy_"),
    )
    setattr(
        op_conf.sparse_cross_entropy_conf,
        "prediction",
        softmax(logits).logical_blob_name,
    )
    setattr(
        op_conf.sparse_cross_entropy_conf, "label", labels.logical_blob_name
    )
    setattr(op_conf.sparse_cross_entropy_conf, "out", "out")
    compile_context.CurJobAddOp(op_conf)
    lbi = logical_blob_id_util.LogicalBlobId()
    lbi.op_name = op_conf.name
    lbi.blob_name = "out"
    return remote_blob_util.RemoteBlob(lbi)


def _GetSequence(value, n, name):
    """Formats value from input"""
    if value is None:
        value = [1]
    elif not isinstance(value, collections.Sized):
        value = [value]

    current_n = len(value)
    if current_n == 1:
        return list(value * n)
    elif current_n == n:
        return list(value)
    else:
        raise ValueError(
            "{} should be of length 1 or {} but was {}".format(
                name, n, current_n
            )
        )


@oneflow_export("nn.dropout")
def dropout(x, noise_shape=None, seed=None, name=None, rate=None):
    op_conf = op_conf_util.OperatorConf()
    if name is None:
        op_conf.name = id_util.UniqueStr("Dropout_")
    else:
        op_conf.name = name
    setattr(op_conf.dropout_conf, "in", x.logical_blob_name)
    setattr(op_conf.dropout_conf, "out", "out")
    if noise_shape is not None:
        assert isinstance(noise_shape, (list, tuple))
        op_conf.dropout_conf.noise_shape.dim.extend(list(noise_shape))
    if seed is not None:
        setattr(op_conf.dropout_conf, "seed", seed)
    assert rate is not None
    setattr(op_conf.dropout_conf, "rate", rate)
    compile_context.CurJobAddOp(op_conf)
    lbi = logical_blob_id_util.LogicalBlobId()
    lbi.op_name = op_conf.name
    lbi.blob_name = "out"
    return remote_blob_util.RemoteBlob(lbi)


@oneflow_export("nn.conv2d_transpose")
def deconv2d(
    value=None,
    filter=None,
    output_shape=None,
    strides=None,
    padding='SAME',
    data_format='NHWC',
    name=None,
    input=None,
    filters=None,
    dilations=None
):
  """2d transposed convolution
  Args:
    value: 4-d `Blob`
    filter: filter of transposed convolution, usually a variable
    output_shape: Not supported yet
    strides: `int` or `int list`
    padding: `'VALID'` or `'SAME'`
    data_format: `'NHWC'` or `'NCHW'`
    name: This operator's name
    input: Alias for value
    filters: Alias for filter
    dilations: Not supported yet
  Returns:
    A `Blob` with the same type as `value`.
  Raises:
    ValueError: shapes of `filter` and `input` must match.
  """
  assert value is not None ^ input is not None, "only one of input and value could be not None"
  assert output_shape is None, "output_shape not supported yet"
  assert dilations is None, "dilations not supported yet"
  assert len(input.static_shape) == 4
  assert len(filters.static_shape) == 4

   if isinstance(strides, (list, tuple)):
        assert len(strides) == 2, ValueError(
            "strides length must be 2 when passed as a list."
        )
    elif isinstance(strides, int):
        strides = [strides, strides]
    else:
        raise ValueError("strides must be an int or a list.")

    if padding.upper() != "SAME" and padding.upper() != "VALID":
        raise ValueError('padding must be "SAME" or "VALID".')

    if data_format.upper() != "NCHW" and data_format.upper() != "NHWC":
        raise ValueError('data_format must be "NHWC" or "NCHW".')

    channel_pos = (
        "channels_first" if data_format.startswith("NC") else "channels_last"
    )

    op_conf = op_conf_util.OperatorConf()
    setattr(op_conf, "name",
            name if name is not None else id_util.UniqueStr("Deconv2d_"))
    setattr(op_conf.deconv_conf, "in", input.logical_blob_name)
    op_conf.deconv_conf.out = "out"
    op_conf.deconv_conf.filter = filters.logical_blob_name
    op_conf.deconv_conf.filters = filters.static_shape[0]
    op_conf.deconv_conf.conv_conf.padding = padding.lower()
    op_conf.deconv_conf.conv_conf.data_format = channel_pos
    if channel_pos == "channels_first":
        op_conf.deconv_conf.conv_conf.kernel_size.extend(
            filters.static_shape[2:4])
    elif channel_pos == "channels_last":
        op_conf.deconv_conf.conv_conf.kernel_size.extend(
            filters.static_shape[-3:-1])
    else:
        raise ValueError("invalid data_format")
    op_conf.deconv_conf.conv_conf.strides.extend(strides)
    op_conf.deconv_conf.conv_conf.dilation_rate.extend(dilations)
    op_conf.deconv_conf.use_bias = False

    compile_context.CurJobAddOp(op_conf)
    lbi = logical_blob_id_util.LogicalBlobId()
    lbi.op_name = op_conf.name
    lbi.blob_name = "out"
    return remote_blob_util.RemoteBlob(lbi)
