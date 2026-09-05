/* api.js —— 与 C 后端通信：JSON 请求 + SSE 流式读取 */

/* 把字面 "\n" 转成真实换行，杜绝任何地方显示 "\\n" */
function normNewline(s) {
  return String(s == null ? '' : s).replace(/\\n/g, '\n');
}

const Api = {
  async json(path, body) {
    const opt = { method: 'GET', headers: { 'Accept': 'application/json' } };
    if (body) {
      opt.method = 'POST';
      opt.headers['Content-Type'] = 'application/json';
      opt.body = JSON.stringify(body);
    }
    const res = await fetch(path, opt);
    const text = await res.text();
    let data = {};
    try { data = JSON.parse(text); } catch (e) { data = { ok: false, error: text.slice(0, 200) }; }
    if (!res.ok && !data.error) data.error = 'HTTP ' + res.status;
    return data;
  },

  /* 流式 POST：把 /api/chat 的 SSE 流逐块喂给 onData({reasoning, content})。
     返回一个 { cancel } 句柄。 */
  stream(path, body, onData, onDone, onError) {
    const controller = new AbortController();
    const handle = { cancel: () => controller.abort() };
    (async () => {
      try {
        const res = await fetch(path, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body),
          signal: controller.signal,
        });
        if (!res.ok || !res.body) {
          onError('HTTP ' + res.status);
          return;
        }
        const reader = res.body.getReader();
        const decoder = new TextDecoder('utf-8');
        let buffer = '';
        let reasoning = '', content = '';
        for (;;) {
          const { done, value } = await reader.read();
          if (done) break;
          buffer += decoder.decode(value, { stream: true });
          let idx;
          while ((idx = buffer.indexOf('\n\n')) >= 0) {
            const block = buffer.slice(0, idx);
            buffer = buffer.slice(idx + 2);
            const lines = block.split('\n');
            for (const line of lines) {
              if (!line.startsWith('data:')) continue;
              const payload = line.slice(5).trim();
              if (!payload) continue;
              if (payload === '[DONE]') { onDone(); return; }
              let obj = null;
              try { obj = JSON.parse(payload); } catch (e) { continue; }
              if (obj.error) { onError(obj.error); return; }
              const d = (obj.choices && obj.choices[0] && obj.choices[0].delta) || {};
              const r = d.reasoning_content || '';
              const c = d.content || '';
              if (r) reasoning += r;
              if (c) content += c;
              if (r || c) onData({ reasoning: r, content: c, fullReasoning: reasoning, fullContent: content });
            }
          }
        }
        onDone();
      } catch (e) {
        if (e.name !== 'AbortError') onError(String(e));
      }
    })();
    return handle;
  },
};
