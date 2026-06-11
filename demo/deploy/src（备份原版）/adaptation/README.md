# adaptation

Lightweight CTTA / runtime adaptation module.

For this RK3588 project, keep this module backpropagation-free. It should not
claim to update RKNN/RKLLM weights on the board.

Good candidates:

- dynamic confidence thresholds
- image quality statistics
- lighting or blur based preprocessing selection
- low-confidence sample cache
- false-positive sample cache
- later cloud/PC retraining data export
