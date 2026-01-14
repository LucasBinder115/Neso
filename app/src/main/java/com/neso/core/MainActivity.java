package com.neso.core;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Rect;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.net.Uri;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

public class MainActivity extends Activity implements SurfaceHolder.Callback {

    private static final int PICK_ROM_REQUEST = 1;

    // NES Controller Bits
    private static final int BTN_A = 0;
    private static final int BTN_B = 1;
    private static final int BTN_SELECT = 2;
    private static final int BTN_START = 3;
    private static final int BTN_UP = 4;
    private static final int BTN_DOWN = 5;
    private static final int BTN_LEFT = 6;
    private static final int BTN_RIGHT = 7;

    static {
        System.loadLibrary("neso");
    }

    private SurfaceView gameSurface;
    private Bitmap screenBitmap;
    private int[] pixels = new int[256 * 240];
    private long cpuPtr = 0;
    private Handler handler = new Handler(Looper.getMainLooper());
    private boolean isRunning = false;
    private boolean isPaused = false;

    // Audio
    private AudioTrack audioTrack;
    private Thread audioThread;
    private boolean audioRunning = false;

    // Nativos
    public native long createCpu();

    public native void loadRom(byte[] data);

    public native void stepCpu(long ptr);

    public native void renderFrame(int[] output);

    public native void setButtonState(int button, boolean pressed);

    public native int getAudioSamples(byte[] out);

    public native int getAudioBufferLevel();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        FrameLayout surfaceContainer = new FrameLayout(this);
        gameSurface = new SurfaceView(this);
        gameSurface.getHolder().addCallback(this);

        FrameLayout.LayoutParams surfaceParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        surfaceParams.gravity = Gravity.CENTER;
        surfaceContainer.addView(gameSurface, surfaceParams);
        root.addView(surfaceContainer);

        FrameLayout uiOverlay = new FrameLayout(this);
        root.addView(uiOverlay);
        setupConsoleControls(uiOverlay);

        setContentView(root);

        // AudioTrack Setup (8-bit PCM, Streaming)
        int minBufSize = AudioTrack.getMinBufferSize(44100, AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_8BIT);
        audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, 44100, AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_8BIT, Math.max(minBufSize, 4096), AudioTrack.MODE_STREAM);
        audioTrack.play();
        startAudio();

        screenBitmap = Bitmap.createBitmap(256, 240, Bitmap.Config.ARGB_8888);
        cpuPtr = createCpu();
    }

    private void startAudio() {
        audioRunning = true;
        audioThread = new Thread(() -> {
            byte[] audioBuf = new byte[512];
            boolean warmedUp = false;
            while (audioRunning) {
                if (isPaused || !isRunning) {
                    warmedUp = false;
                    try {
                        Thread.sleep(20);
                    } catch (Exception e) {
                    }
                    continue;
                }

                // Warmup Cushion: avoid starvation crackle
                if (!warmedUp) {
                    if (getAudioBufferLevel() < 30) {
                        try {
                            Thread.sleep(10);
                        } catch (Exception e) {
                        }
                        continue;
                    }
                    warmedUp = true;
                }

                int read = getAudioSamples(audioBuf);
                if (read > 0) {
                    audioTrack.write(audioBuf, 0, read);
                } else {
                    warmedUp = false; // Force re-warmup if we starve
                    try {
                        Thread.sleep(5);
                    } catch (Exception e) {
                    }
                }
            }
        });
        audioThread.setPriority(Thread.MAX_PRIORITY);
        audioThread.start();
    }

    private void setupConsoleControls(FrameLayout layout) {
        Button btnRom = new Button(this);
        btnRom.setText("ROM");
        btnRom.setTextSize(10);
        btnRom.getBackground().setAlpha(128);
        btnRom.setTextColor(Color.WHITE);
        FrameLayout.LayoutParams romParams = new FrameLayout.LayoutParams(140, 80);
        romParams.gravity = Gravity.TOP | Gravity.END;
        romParams.topMargin = 40;
        romParams.rightMargin = 40;
        btnRom.setOnClickListener(v -> pickRom());
        layout.addView(btnRom, romParams);

        ImageView dpad = createControl(R.drawable.ic_dpad, 320, 320);
        anchorView(dpad, Gravity.BOTTOM | Gravity.START, 80, 80);
        dpad.setOnTouchListener(handleDPad());
        layout.addView(dpad);

        ImageView btnB = createControl(R.drawable.ic_button_b, 160, 160);
        anchorView(btnB, Gravity.BOTTOM | Gravity.END, 280, 80);
        btnB.setOnTouchListener(handleButton(BTN_B));
        layout.addView(btnB);

        ImageView btnA = createControl(R.drawable.ic_button_a, 160, 160);
        anchorView(btnA, Gravity.BOTTOM | Gravity.END, 80, 180);
        btnA.setOnTouchListener(handleButton(BTN_A));
        layout.addView(btnA);

        ImageView btnSelect = createControl(R.drawable.ic_button_pill, 180, 60);
        anchorView(btnSelect, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL, -110, 80);
        btnSelect.setOnTouchListener(handleButton(BTN_SELECT));
        layout.addView(btnSelect);

        ImageView btnStart = createControl(R.drawable.ic_button_pill, 180, 60);
        anchorView(btnStart, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL, 110, 80);
        btnStart.setOnTouchListener(handleButton(BTN_START));
        layout.addView(btnStart);
    }

    private ImageView createControl(int resId, int w, int h) {
        ImageView iv = new ImageView(this);
        iv.setImageResource(resId);
        iv.setLayoutParams(new FrameLayout.LayoutParams(w, h));
        iv.setAlpha(0.6f);
        return iv;
    }

    private void anchorView(View v, int gravity, int horizontalMargin, int verticalMargin) {
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) v.getLayoutParams();
        lp.gravity = gravity;
        if ((gravity & Gravity.START) == Gravity.START)
            lp.leftMargin = horizontalMargin;
        if ((gravity & Gravity.END) == Gravity.END)
            lp.rightMargin = horizontalMargin;
        if ((gravity & Gravity.BOTTOM) == Gravity.BOTTOM)
            lp.bottomMargin = verticalMargin;
        if ((gravity & Gravity.TOP) == Gravity.TOP)
            lp.topMargin = verticalMargin;
        if ((gravity & Gravity.CENTER_HORIZONTAL) == Gravity.CENTER_HORIZONTAL)
            lp.leftMargin = horizontalMargin;
    }

    private View.OnTouchListener handleDPad() {
        return (v, event) -> {
            boolean down = event.getAction() != MotionEvent.ACTION_UP;
            v.setAlpha(down ? 1.0f : 0.6f);
            if (!down) {
                setButtonState(BTN_UP, false);
                setButtonState(BTN_DOWN, false);
                setButtonState(BTN_LEFT, false);
                setButtonState(BTN_RIGHT, false);
                return true;
            }
            float x = (event.getX() / v.getWidth()) - 0.5f;
            float y = (event.getY() / v.getHeight()) - 0.5f;
            setButtonState(BTN_UP, false);
            setButtonState(BTN_DOWN, false);
            setButtonState(BTN_LEFT, false);
            setButtonState(BTN_RIGHT, false);
            if (Math.abs(x) > Math.abs(y)) {
                if (x < 0)
                    setButtonState(BTN_LEFT, true);
                else
                    setButtonState(BTN_RIGHT, true);
            } else {
                if (y < 0)
                    setButtonState(BTN_UP, true);
                else
                    setButtonState(BTN_DOWN, true);
            }
            return true;
        };
    }

    private View.OnTouchListener handleButton(int bit) {
        return (v, event) -> {
            boolean pressed = event.getAction() == MotionEvent.ACTION_DOWN
                    || event.getAction() == MotionEvent.ACTION_MOVE;
            boolean released = event.getAction() == MotionEvent.ACTION_UP
                    || event.getAction() == MotionEvent.ACTION_CANCEL;
            if (pressed) {
                setButtonState(bit, true);
                v.setAlpha(1.0f);
            } else if (released) {
                setButtonState(bit, false);
                v.setAlpha(0.6f);
            }
            return true;
        };
    }

    private void pickRom() {
        isPaused = true;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        isPaused = false;
        if (requestCode == PICK_ROM_REQUEST && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            try {
                InputStream is = getContentResolver().openInputStream(uri);
                ByteArrayOutputStream buffer = new ByteArrayOutputStream();
                int nRead;
                byte[] temp = new byte[16384];
                while ((nRead = is.read(temp, 0, temp.length)) != -1)
                    buffer.write(temp, 0, nRead);
                byte[] romData = buffer.toByteArray();
                loadRom(romData);
            } catch (Exception e) {
            }
        }
    }

    private void emuLoop() {
        if (!isRunning || isPaused) {
            handler.postDelayed(this::emuLoop, 16);
            return;
        }
        stepCpu(cpuPtr);
        renderFrame(pixels);
        screenBitmap.setPixels(pixels, 0, 256, 0, 0, 256, 240);
        SurfaceHolder holder = gameSurface.getHolder();
        Canvas canvas = holder.lockCanvas();
        if (canvas != null) {
            float scale = Math.min((float) canvas.getWidth() / 256f, (float) canvas.getHeight() / 240f);
            int w = (int) (256 * scale), h = (int) (240 * scale);
            int left = (canvas.getWidth() - w) / 2, top = (canvas.getHeight() - h) / 2;
            canvas.drawColor(Color.BLACK);
            canvas.drawBitmap(screenBitmap, null, new Rect(left, top, left + w, top + h), null);
            holder.unlockCanvasAndPost(canvas);
        }
        handler.postDelayed(this::emuLoop, 16);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        isRunning = true;
        handler.post(this::emuLoop);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        isRunning = false;
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        audioRunning = false;
        if (audioTrack != null) {
            audioTrack.stop();
            audioTrack.release();
        }
    }
}