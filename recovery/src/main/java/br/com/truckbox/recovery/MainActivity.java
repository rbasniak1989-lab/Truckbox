package br.com.truckbox.recovery;

import android.app.Activity;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private volatile boolean running = false;
    private volatile String lastWorkingBase = null;

    private TextView status;
    private TextView core;
    private TextView queue;
    private TextView saved;
    private TextView ack;
    private Button toggle;

    private File spoolFile;
    private android.content.SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        prefs = getSharedPreferences("recovery", MODE_PRIVATE);
        spoolFile = new File(getFilesDir(), "truckbox_recovery_spool.ndjson");

        int pad = dp(18);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);

        TextView title = text("TruckBox Recovery", 26, true);
        TextView subtitle = text("Salva a fila do ESP32 no celular antes de dar ACK.", 15, false);
        status = text("Parado", 18, true);
        core = text("ESP32: —", 17, false);
        queue = text("Fila ESP32: —", 17, false);
        saved = text("", 17, false);
        ack = text("Último ACK local: —", 17, false);
        toggle = new Button(this);
        toggle.setText("INICIAR RECUPERAÇÃO");

        box.addView(title);
        box.addView(subtitle);
        spacer(box, 18);
        box.addView(status);
        spacer(box, 10);
        box.addView(core);
        box.addView(queue);
        box.addView(saved);
        box.addView(ack);
        spacer(box, 20);
        box.addView(toggle, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView note = text(
                "Segurança: cada lote é gravado no armazenamento interno e sincronizado no disco antes do ACK. " +
                "Se o ACK falhar, o mesmo lote é mantido e o Recovery tenta novamente.",
                14, false);
        spacer(box, 18);
        box.addView(note);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(box);
        setContentView(scroll);

        updateSavedLabel();

        toggle.setOnClickListener(v -> {
            if (running) stopRecovery();
            else startRecovery();
        });
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(sp);
        t.setPadding(0, dp(6), 0, dp(6));
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return t;
    }

    private void spacer(LinearLayout box, int h) {
        TextView s = new TextView(this);
        box.addView(s, new LinearLayout.LayoutParams(1, dp(h)));
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private void startRecovery() {
        running = true;
        toggle.setText("PARAR");
        status.setText("Iniciando...");
        executor.execute(this::drainLoop);
    }

    private void stopRecovery() {
        running = false;
        toggle.setText("INICIAR RECUPERAÇÃO");
        status.setText("Parando...");
    }

    private void drainLoop() {
        long savedMax = prefs.getLong("saved_max_seq", 0L);

        while (running && !Thread.currentThread().isInterrupted()) {
            try {
                Network wifi = currentWifiNetwork();
                if (wifi == null) {
                    ui("Aguardando Wi-Fi do TruckBox...", "ESP32: OFFLINE", null, null);
                    SystemClock.sleep(1000);
                    continue;
                }

                JSONObject batch = fetchBatch(wifi);
                if (batch == null) {
                    ui("Core não encontrado. Tentando novamente...", "ESP32: OFFLINE", null, null);
                    SystemClock.sleep(1000);
                    continue;
                }

                int count = batch.optInt("count", 0);
                long pending = batch.optLong("pending_bytes", 0L);
                long lastAck = batch.optLong("last_ack_seq", 0L);
                long maxSeq = batch.optLong("max_seq", 0L);

                ui(
                    count > 0 ? "Recuperando fila..." : "Fila vazia • monitorando",
                    "ESP32: ONLINE",
                    String.format(Locale.US, "Fila ESP32: %.1f KB • %d amostras no lote", pending / 1024.0, count),
                    "Último ACK local: " + lastAck
                );

                if (count <= 0 || maxSeq <= 0) {
                    SystemClock.sleep(500);
                    continue;
                }

                if (maxSeq > savedMax) {
                    saveBatchDurably(batch);
                    if (!prefs.edit().putLong("saved_max_seq", maxSeq).commit()) {
                        throw new IllegalStateException("Falha ao registrar sequência salva");
                    }
                    savedMax = maxSeq;
                    runOnUiThread(this::updateSavedLabel);
                }

                if (!ackBatch(wifi, maxSeq)) {
                    uiStatus("Lote salvo no celular; ACK ao ESP32 falhou. Tentando novamente...");
                    SystemClock.sleep(500);
                    continue;
                }

                uiStatus("Lote salvo + ACK confirmado. Liberando memória...");
                SystemClock.sleep(pending > 12_000L ? 80 : 250);
            } catch (Throwable e) {
                uiStatus("Erro: " + shortMessage(e));
                SystemClock.sleep(1000);
            }
        }

        runOnUiThread(() -> {
            toggle.setText("INICIAR RECUPERAÇÃO");
            if (!running) status.setText("Parado");
        });
    }

    private Network currentWifiNetwork() {
        ConnectivityManager cm = getSystemService(ConnectivityManager.class);
        if (cm == null) return null;
        for (Network n : cm.getAllNetworks()) {
            NetworkCapabilities c = cm.getNetworkCapabilities(n);
            if (c != null && c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return n;
        }
        return null;
    }

    private JSONObject fetchBatch(Network wifi) {
        String[] bases = orderedBases();
        for (String base : bases) {
            HttpURLConnection c = null;
            try {
                URL url = new URL(base + "/api/gateway/batch?limit=8&nocache=" + System.currentTimeMillis());
                c = (HttpURLConnection) wifi.openConnection(url);
                c.setRequestMethod("GET");
                c.setConnectTimeout(2000);
                c.setReadTimeout(4000);
                c.setUseCaches(false);
                c.setRequestProperty("Connection", "close");
                int code = c.getResponseCode();
                if (code < 200 || code >= 300) continue;
                String body = new String(c.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
                JSONObject j = new JSONObject(body);
                if (!j.optBoolean("ok", false)) continue;
                lastWorkingBase = base;
                return j;
            } catch (Throwable ignored) {
            } finally {
                if (c != null) c.disconnect();
            }
        }
        return null;
    }

    private boolean ackBatch(Network wifi, long seq) {
        String form;
        try {
            form = "ack_seq=" + URLEncoder.encode(Long.toString(seq), StandardCharsets.UTF_8.name());
        } catch (Throwable e) {
            return false;
        }

        for (String base : orderedBases()) {
            for (int attempt = 0; attempt < 4; attempt++) {
                HttpURLConnection c = null;
                try {
                    c = (HttpURLConnection) wifi.openConnection(new URL(base + "/api/gateway/ack"));
                    c.setRequestMethod("POST");
                    c.setDoOutput(true);
                    c.setConnectTimeout(3000);
                    c.setReadTimeout(18000);
                    c.setUseCaches(false);
                    c.setRequestProperty("Connection", "close");
                    c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
                    c.getOutputStream().write(form.getBytes(StandardCharsets.UTF_8));
                    int code = c.getResponseCode();
                    if (code >= 200 && code < 300) {
                        lastWorkingBase = base;
                        return true;
                    }
                } catch (Throwable ignored) {
                } finally {
                    if (c != null) c.disconnect();
                }
                SystemClock.sleep(250);
            }
        }
        return false;
    }

    private String[] orderedBases() {
        if (lastWorkingBase == null) {
            return new String[]{"http://truckbox.local", "http://192.168.4.1"};
        }
        String other = lastWorkingBase.contains("truckbox.local")
                ? "http://192.168.4.1"
                : "http://truckbox.local";
        return new String[]{lastWorkingBase, other};
    }

    private void saveBatchDurably(JSONObject batch) throws Exception {
        JSONObject record = new JSONObject();
        record.put("saved_at_ms", System.currentTimeMillis());
        record.put("batch", batch);

        byte[] line = (record.toString() + "\n").getBytes(StandardCharsets.UTF_8);
        if (spoolFile.getParentFile() != null && !spoolFile.getParentFile().exists()) {
            if (!spoolFile.getParentFile().mkdirs()) throw new IllegalStateException("Falha ao criar pasta local");
        }
        if (spoolFile.getUsableSpace() < line.length + 10L * 1024L * 1024L) {
            throw new IllegalStateException("Pouco espaço livre no celular");
        }

        try (FileOutputStream out = new FileOutputStream(spoolFile, true)) {
            out.write(line);
            out.flush();
            out.getFD().sync();
        }
    }

    private void updateSavedLabel() {
        long max = prefs.getLong("saved_max_seq", 0L);
        double kb = spoolFile.exists() ? spoolFile.length() / 1024.0 : 0.0;
        saved.setText(String.format(Locale.US,
                "Salvo no celular: %.1f KB • até seq %d", kb, max));
    }

    private void uiStatus(String s) {
        runOnUiThread(() -> status.setText(s));
    }

    private void ui(String s, String c, String q, String a) {
        runOnUiThread(() -> {
            status.setText(s);
            core.setText(c);
            if (q != null) queue.setText(q);
            if (a != null) ack.setText(a);
        });
    }

    private String shortMessage(Throwable e) {
        String m = e.getMessage();
        if (m == null || m.isBlank()) m = e.getClass().getSimpleName();
        return m.length() > 180 ? m.substring(0, 180) : m;
    }

    @Override
    protected void onDestroy() {
        running = false;
        executor.shutdownNow();
        super.onDestroy();
    }
}
