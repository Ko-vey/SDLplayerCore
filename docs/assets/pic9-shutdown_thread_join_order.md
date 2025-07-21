# 停止线程的顺序-示意流程图-顺着数据流

```mermaid
graph LR
    A[解封装线程<br>（Demux Thread）] --> B[音/视频解码线程<br>（Decode Threads）]
    B --> C[音/视频渲染线程<br>（Render Threads）]
```
