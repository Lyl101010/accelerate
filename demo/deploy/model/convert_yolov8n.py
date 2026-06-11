from rknn.api import RKNN

ONNX_MODEL = "yolov8n.onnx"
RKNN_MODEL = "yolov8n_rk3588.rknn"

def main():
    rknn = RKNN(verbose=True)

    print("1. Config RKNN")
    ret = rknn.config(
        target_platform="rk3588",
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]]
    )
    if ret != 0:
        print("Config failed")
        return ret

    print("2. Load ONNX")
    ret = rknn.load_onnx(model=ONNX_MODEL)
    if ret != 0:
        print("Load ONNX failed")
        return ret

    print("3. Build RKNN")
    ret = rknn.build(do_quantization=False)
    if ret != 0:
        print("Build RKNN failed")
        return ret

    print("4. Export RKNN")
    ret = rknn.export_rknn(RKNN_MODEL)
    if ret != 0:
        print("Export RKNN failed")
        return ret

    print("Done:", RKNN_MODEL)
    rknn.release()
    return 0

if __name__ == "__main__":
    main()
