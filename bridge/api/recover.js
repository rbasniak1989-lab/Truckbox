export default async function handler(req, res) {
  if (req.method === "GET") {
    return res.status(200).json({ ok: true, service: "truckbox-recovery-bridge" });
  }
  if (req.method !== "POST") {
    res.setHeader("Allow", "GET, POST");
    return res.status(405).json({ error: "method_not_allowed" });
  }

  const recoveryKey = process.env.TRUCKBOX_RECOVERY_KEY;
  if (!recoveryKey) {
    return res.status(503).json({ error: "bridge_not_configured" });
  }

  try {
    const upstream = await fetch(
      "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-recovery-ingest",
      {
        method: "POST",
        headers: {
          "content-type": "application/json",
          "x-truckbox-recovery-key": recoveryKey,
        },
        body: JSON.stringify(req.body ?? {}),
        signal: AbortSignal.timeout(20000),
      }
    );

    const text = await upstream.text();
    res.status(upstream.status);
    res.setHeader("content-type", upstream.headers.get("content-type") || "application/json");
    return res.send(text);
  } catch (error) {
    return res.status(502).json({
      error: "upstream_unreachable",
      detail: String(error?.message || error).slice(0, 200),
    });
  }
}
