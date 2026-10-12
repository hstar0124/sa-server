# deploy/k3s

K3s 매니페스트는 6단계(Outbox·Kafka·Worker)에서 작성한다 (ADR-0002). 그 전까지 개발 루프는 `deploy/compose/`다.

도입 시 결정할 것: swap 처리(`fail-swap-on=false` vs swapoff), local-path PV 경로, namespace(`game`, `data`, `observability`), Gateway L4 노출 방식(NodePort).
