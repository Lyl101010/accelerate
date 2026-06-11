#include "fusion_inference.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "rknn_api.h"

namespace {

std::vector<unsigned char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open RKNN model: " + path);
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("Empty RKNN model: " + path);
    }

    std::vector<unsigned char> data(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Failed to read RKNN model: " + path);
    }
    return data;
}

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

unsigned char toU8(float value) {
    value = clamp01(value);
    return static_cast<unsigned char>(std::round(value * 255.0f));
}

cv::Mat normalizeToFloat(const cv::Mat& src) {
    cv::Mat dst;
    if (src.depth() == CV_8U) {
        src.convertTo(dst, CV_32F, 1.0 / 255.0);
    } else if (src.depth() == CV_16U) {
        src.convertTo(dst, CV_32F, 1.0 / 65535.0);
    } else if (src.depth() == CV_32F) {
        dst = src.clone();
        double max_value = 0.0;
        cv::minMaxLoc(dst.reshape(1), nullptr, &max_value);
        if (max_value > 1.0) {
            dst *= 1.0f / 255.0f;
        }
    } else {
        src.convertTo(dst, CV_32F);
        double max_value = 0.0;
        cv::minMaxLoc(dst.reshape(1), nullptr, &max_value);
        if (max_value > 1.0) {
            dst *= 1.0f / 255.0f;
        }
    }
    cv::min(dst, 1.0, dst);
    cv::max(dst, 0.0, dst);
    return dst;
}

int tensorHeight(const rknn_tensor_attr& attr) {
    if (attr.n_dims < 4) {
        return 0;
    }
    if (attr.fmt == RKNN_TENSOR_NHWC) {
        return attr.dims[1];
    }
    return attr.dims[2];
}

int tensorWidth(const rknn_tensor_attr& attr) {
    if (attr.n_dims < 4) {
        return 0;
    }
    if (attr.fmt == RKNN_TENSOR_NHWC) {
        return attr.dims[2];
    }
    return attr.dims[3];
}

void printTensorAttr(const char* prefix, const rknn_tensor_attr& attr) {
    std::cout << prefix << " index=" << attr.index << " name=" << attr.name << " dims=[";
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        std::cout << attr.dims[i] << (i + 1 == attr.n_dims ? "" : ",");
    }
    std::cout << "] type=" << attr.type << " fmt=" << attr.fmt << "\n";
}

}  // namespace

struct SeAFusion::Impl {
    explicit Impl(Options options_in) : options(options_in) {}

    ~Impl() {
        if (ctx != 0) {
            rknn_destroy(ctx);
        }
    }

    Options options;
    rknn_context ctx = 0;
    rknn_input_output_num io_num{};
    std::vector<rknn_tensor_attr> input_attrs;
    std::vector<rknn_tensor_attr> output_attrs;
    int input_h = 0;
    int input_w = 0;
    bool loaded = false;

    void queryModel() {
        int ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if (ret != RKNN_SUCC) {
            throw std::runtime_error("rknn_query(RKNN_QUERY_IN_OUT_NUM) failed: " + std::to_string(ret));
        }
        if (io_num.n_input != 2 || io_num.n_output < 1) {
            throw std::runtime_error("SeAFusion RKNN model must have 2 inputs and at least 1 output");
        }

        input_attrs.assign(io_num.n_input, rknn_tensor_attr{});
        for (uint32_t i = 0; i < io_num.n_input; ++i) {
            input_attrs[i].index = i;
            ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
            if (ret != RKNN_SUCC) {
                throw std::runtime_error("rknn_query(RKNN_QUERY_INPUT_ATTR) failed: " + std::to_string(ret));
            }
            printTensorAttr("input", input_attrs[i]);
        }

        output_attrs.assign(io_num.n_output, rknn_tensor_attr{});
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            output_attrs[i].index = i;
            ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
            if (ret != RKNN_SUCC) {
                throw std::runtime_error("rknn_query(RKNN_QUERY_OUTPUT_ATTR) failed: " + std::to_string(ret));
            }
            printTensorAttr("output", output_attrs[i]);
        }

        input_h = tensorHeight(input_attrs[0]);
        input_w = tensorWidth(input_attrs[0]);
        if (input_h <= 0 || input_w <= 0) {
            throw std::runtime_error("Unsupported RKNN input shape");
        }
    }

    cv::Mat prepareVisible(const cv::Mat& vis_bgr, cv::Mat& cb, cv::Mat& cr) const {
        if (vis_bgr.empty()) {
            throw std::invalid_argument("Visible frame is empty");
        }
        if (vis_bgr.channels() != 3) {
            throw std::invalid_argument("Visible frame must be BGR with 3 channels");
        }

        cv::Mat resized = vis_bgr;
        if (resized.rows != input_h || resized.cols != input_w) {
            if (!options.resize_to_model) {
                throw std::invalid_argument("Visible frame size does not match model input size");
            }
            cv::resize(vis_bgr, resized, cv::Size(input_w, input_h), 0, 0, cv::INTER_LINEAR);
        }

        cv::Mat bgr_float = normalizeToFloat(resized);
        std::vector<cv::Mat> bgr;
        cv::split(bgr_float, bgr);

        cv::Mat y(input_h, input_w, CV_32FC1);
        cb.create(input_h, input_w, CV_32FC1);
        cr.create(input_h, input_w, CV_32FC1);

        for (int row = 0; row < input_h; ++row) {
            const float* b_ptr = bgr[0].ptr<float>(row);
            const float* g_ptr = bgr[1].ptr<float>(row);
            const float* r_ptr = bgr[2].ptr<float>(row);
            float* y_ptr = y.ptr<float>(row);
            float* cb_ptr = cb.ptr<float>(row);
            float* cr_ptr = cr.ptr<float>(row);
            for (int col = 0; col < input_w; ++col) {
                const float b = b_ptr[col];
                const float g = g_ptr[col];
                const float r = r_ptr[col];
                const float yy = clamp01(0.299f * r + 0.587f * g + 0.114f * b);
                y_ptr[col] = yy;
                cr_ptr[col] = clamp01((r - yy) * 0.713f + 0.5f);
                cb_ptr[col] = clamp01((b - yy) * 0.564f + 0.5f);
            }
        }
        return y;
    }

    cv::Mat prepareIr(const cv::Mat& ir_frame) const {
        if (ir_frame.empty()) {
            throw std::invalid_argument("Infrared frame is empty");
        }

        cv::Mat gray;
        if (ir_frame.channels() == 1) {
            gray = ir_frame;
        } else if (ir_frame.channels() == 3) {
            cv::cvtColor(ir_frame, gray, cv::COLOR_BGR2GRAY);
        } else {
            throw std::invalid_argument("Infrared frame must have 1 or 3 channels");
        }

        if (gray.rows != input_h || gray.cols != input_w) {
            if (!options.resize_to_model) {
                throw std::invalid_argument("Infrared frame size does not match model input size");
            }
            cv::resize(gray, gray, cv::Size(input_w, input_h), 0, 0, cv::INTER_LINEAR);
        }

        return normalizeToFloat(gray);
    }

    cv::Mat composeBgr(const float* fused_y, size_t fused_count, const cv::Mat& cb, const cv::Mat& cr) const {
        const size_t expected = static_cast<size_t>(input_h) * static_cast<size_t>(input_w);
        if (fused_count < expected) {
            throw std::runtime_error("RKNN output is smaller than expected");
        }

        cv::Mat fused_bgr(input_h, input_w, CV_8UC3);
        for (int row = 0; row < input_h; ++row) {
            const float* cb_ptr = cb.ptr<float>(row);
            const float* cr_ptr = cr.ptr<float>(row);
            cv::Vec3b* out_ptr = fused_bgr.ptr<cv::Vec3b>(row);
            for (int col = 0; col < input_w; ++col) {
                const size_t idx = static_cast<size_t>(row) * input_w + col;
                const float y = clamp01(fused_y[idx]);
                const float cbv = cb_ptr[col] - 0.5f;
                const float crv = cr_ptr[col] - 0.5f;

                const float r = y + 1.403f * crv;
                const float g = y - 0.714f * crv - 0.344f * cbv;
                const float b = y + 1.773f * cbv;
                out_ptr[col] = cv::Vec3b(toU8(b), toU8(g), toU8(r));
            }
        }
        return fused_bgr;
    }
};

SeAFusion::SeAFusion() : SeAFusion(Options{}) {}

SeAFusion::SeAFusion(Options options) : impl_(new Impl(options)) {}

SeAFusion::~SeAFusion() = default;

bool SeAFusion::load(const std::string& rknn_model_path) {
    try {
        const std::vector<unsigned char> model_data = readFile(rknn_model_path);
        if (impl_->ctx != 0) {
            rknn_destroy(impl_->ctx);
            impl_->ctx = 0;
        }

        const int ret = rknn_init(
            &impl_->ctx,
            const_cast<unsigned char*>(model_data.data()),
            static_cast<uint32_t>(model_data.size()),
            0,
            nullptr);
        if (ret != RKNN_SUCC) {
            std::cerr << "rknn_init failed: " << ret << "\n";
            return false;
        }

        impl_->queryModel();
        impl_->loaded = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load SeAFusion RKNN model: " << e.what() << "\n";
        return false;
    }
}

bool SeAFusion::isLoaded() const {
    return impl_->loaded;
}

int SeAFusion::inputWidth() const {
    return impl_->input_w;
}

int SeAFusion::inputHeight() const {
    return impl_->input_h;
}

cv::Mat SeAFusion::fuse(const cv::Mat& vis_bgr, const cv::Mat& ir_frame) {
    if (!impl_->loaded) {
        throw std::runtime_error("SeAFusion model is not loaded");
    }

    cv::Mat cb;
    cv::Mat cr;
    cv::Mat vi_y = impl_->prepareVisible(vis_bgr, cb, cr);
    cv::Mat ir = impl_->prepareIr(ir_frame);

    vi_y = vi_y.isContinuous() ? vi_y : vi_y.clone();
    ir = ir.isContinuous() ? ir : ir.clone();

    std::vector<rknn_input> inputs(2);
    std::memset(inputs.data(), 0, sizeof(rknn_input) * inputs.size());

    inputs[0].index = 0;
    inputs[0].buf = vi_y.data;
    inputs[0].size = static_cast<uint32_t>(vi_y.total() * sizeof(float));
    inputs[0].type = RKNN_TENSOR_FLOAT32;
    inputs[0].fmt = impl_->input_attrs[0].fmt;
    inputs[0].pass_through = 0;

    inputs[1].index = 1;
    inputs[1].buf = ir.data;
    inputs[1].size = static_cast<uint32_t>(ir.total() * sizeof(float));
    inputs[1].type = RKNN_TENSOR_FLOAT32;
    inputs[1].fmt = impl_->input_attrs[1].fmt;
    inputs[1].pass_through = 0;

    int ret = rknn_inputs_set(impl_->ctx, static_cast<uint32_t>(inputs.size()), inputs.data());
    if (ret != RKNN_SUCC) {
        throw std::runtime_error("rknn_inputs_set failed: " + std::to_string(ret));
    }

    ret = rknn_run(impl_->ctx, nullptr);
    if (ret != RKNN_SUCC) {
        throw std::runtime_error("rknn_run failed: " + std::to_string(ret));
    }

    std::vector<rknn_output> outputs(1);
    std::memset(outputs.data(), 0, sizeof(rknn_output) * outputs.size());
    outputs[0].index = 0;
    outputs[0].want_float = 1;

    ret = rknn_outputs_get(impl_->ctx, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
        throw std::runtime_error("rknn_outputs_get failed: " + std::to_string(ret));
    }

    cv::Mat fused_bgr;
    try {
        const auto* fused_y = static_cast<const float*>(outputs[0].buf);
        const size_t fused_count = outputs[0].size / sizeof(float);
        fused_bgr = impl_->composeBgr(fused_y, fused_count, cb, cr);
    } catch (...) {
        rknn_outputs_release(impl_->ctx, static_cast<uint32_t>(outputs.size()), outputs.data());
        throw;
    }

    rknn_outputs_release(impl_->ctx, static_cast<uint32_t>(outputs.size()), outputs.data());
    return fused_bgr;
}
