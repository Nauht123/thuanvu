document.addEventListener('DOMContentLoaded', function () {
    const spo2GaugeFill = document.querySelector('.gauge-fill');
    const spo2Value = document.getElementById('spo2-value');
    const hrValue = document.getElementById('hr-value');
    const lowerThreshold = document.getElementById('lower-threshold');
    const upperThreshold = document.getElementById('upper-threshold');
    const modeTitle = document.getElementById('mode-title');
    const manualContent = document.getElementById('manual-content');
    const autoContent = document.getElementById('auto-content');
    const sendDataBtn = document.getElementById('send-to-device');
    const userSelect = document.getElementById('user-select');
    const alertOnBtn = document.querySelector('.manual-content .on');
    const alertOffBtn = document.querySelector('.manual-content .off');
    const MAX_CHART_ENTRIES = 10;

    let chartInstance = null;
    let latestTimestamps = {};
    let currentUserID = null;
    const POLLING_INTERVAL = 10000; // 10 seconds

    // ------------------ GAUGE ------------------
    function updateSpo2Gauge(value) {
        const percentage = parseFloat(value) || 0;
        const clampedValue = Math.min(Math.max(percentage, 0), 100);
        const arcLength = 251.2;
        const offset = arcLength * (1 - clampedValue / 100);
        spo2GaugeFill.style.strokeDashoffset = offset;
        spo2Value.textContent = `${clampedValue}%`;
    }

    // ------------------ FETCH DATA ------------------
    async function fetchLatestData(userID) {
        try {
            const response = await fetch(`http://localhost:3000/monitoring/latest?userID=${userID}`);
            if (!response.ok) throw new Error('Failed to fetch latest data');
            const data = await response.json();
            return data.length > 0 ? data[0] : null;
        } catch (err) {
            console.error(err);
            return null;
        }
    }

    async function fetchHistory(userID, since = null, limit = MAX_CHART_ENTRIES) {
        try {
            let url = `http://localhost:3000/monitoring/history/${userID}?limit=${limit}`;
            if (since) url += `&since=${encodeURIComponent(since)}`;
            const response = await fetch(url);
            if (!response.ok) throw new Error('Failed to fetch history');
            const history = await response.json();
            return Array.isArray(history) ? history : [];
        } catch (err) {
            console.error(err);
            return [];
        }
    }

    // ------------------ UPDATE UI ------------------
    async function updateUserData(userID) {
        if (!userID) return;
        currentUserID = userID;

        const latestData = await fetchLatestData(userID);
        if (latestData) {
            updateSpo2Gauge(latestData.spO2);
            hrValue.textContent = `${latestData.heartRate} bpm`;
            lowerThreshold.textContent = `${latestData.lowerThreshold}%`;
            upperThreshold.textContent = `${latestData.upperThreshold}%`;
        } else {
            updateSpo2Gauge(95);
            hrValue.textContent = 'N/A';
            lowerThreshold.textContent = 'N/A';
            upperThreshold.textContent = 'N/A';
        }

        updateChart(userID, false);
        updateEmotion(); // cập nhật cảm xúc hiện tại khi load dữ liệu
    }

    // ------------------ ALERT BUTTONS ------------------
    alertOnBtn.addEventListener('click', () => sendAlertCommand('on'));
    alertOffBtn.addEventListener('click', () => sendAlertCommand('off'));

    async function sendAlertCommand(state) {
        try {
            const response = await fetch('http://localhost:3000/control-alert', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ alertState: state })
            });
            const result = await response.json();
            if (!response.ok) throw new Error(result.error || 'Failed to send alert command');
            alert(`Alert turned ${state} successfully!`);
        } catch (err) {
            console.error(err);
            alert(`Error turning alert ${state}: ${err.message}`);
        }
    }

    // ------------------ MODE (CẢM XÚC HIỆN TẠI) ------------------
    modeTitle.textContent = 'CẢM XÚC HIỆN TẠI';
    manualContent.innerHTML = '<p id="emotion-result" style="text-align:center; font-size:24px;">Loading...</p>';
    autoContent.style.display = 'none'; // ẩn automatic mode

    const emotions = ['Happy', 'Sad', 'Angry', 'Relaxed', 'Anxious', 'Neutral'];
    function updateEmotion() {
        const index = Math.floor(Math.random() * emotions.length);
        document.getElementById('emotion-result').textContent = emotions[index];
    }

    // ------------------ CHART (HEART RATE) ------------------
    async function updateChart(userID, incremental = false) {
        if (!userID) return;
        if (!latestTimestamps[userID]) latestTimestamps[userID] = null;

        let since = latestTimestamps[userID];
        if (incremental && chartInstance?.data?.datasets[0]?.timestamps) {
            since = chartInstance.data.datasets[0].timestamps.slice(-1)[0];
        }

        const history = await fetchHistory(userID, since);
        if (!history || history.length === 0) {
            if (!chartInstance || !incremental) initializeChart([], []);
            return;
        }

        latestTimestamps[userID] = history[history.length - 1].recorded;

        const labels = history.map(d => new Date(d.recorded).toLocaleTimeString());
        const data = history.map(d => d.heartRate); // đổi sang nhịp tim

        if (incremental && chartInstance) {
            history.forEach(d => {
                if (!chartInstance.data.datasets[0].timestamps.includes(d.recorded)) {
                    chartInstance.data.labels.push(new Date(d.recorded).toLocaleTimeString());
                    chartInstance.data.datasets[0].data.push(d.heartRate);
                    chartInstance.data.datasets[0].timestamps.push(d.recorded);
                }
            });

            if (chartInstance.data.labels.length > MAX_CHART_ENTRIES) {
                const excess = chartInstance.data.labels.length - MAX_CHART_ENTRIES;
                chartInstance.data.labels.splice(0, excess);
                chartInstance.data.datasets[0].data.splice(0, excess);
                chartInstance.data.datasets[0].timestamps.splice(0, excess);
            }
            chartInstance.update();
            return;
        }

        initializeChart(labels, data);
    }

    function initializeChart(labels, data) {
        const ctx = document.getElementById('stressChart').getContext('2d');
        if (chartInstance) chartInstance.destroy();

        chartInstance = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{
                    label: 'Heart Rate (bpm)',
                    data: data,
                    borderColor: 'rgba(255,255,255,1)',
                    backgroundColor: 'rgba(255,255,255,0.2)',
                    fill: true,
                    tension: 0.4,
                    timestamps: labels.map((_, i) => latestTimestamps[currentUserID])
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { title: { display: true, text: 'Time' }, ticks: { color: 'white' } },
                    y: { title: { display: true, text: 'Heart Rate (bpm)' }, beginAtZero: true, ticks: { color: 'white' } }
                },
                plugins: { legend: { display: true, labels: { color: 'white' } } }
            }
        });
    }

    // ------------------ POLLING ------------------
    function startPolling() {
        setInterval(async () => {
            if (currentUserID) {
                try {
                    await updateUserData(currentUserID);
                    await updateChart(currentUserID, true);
                } catch (err) {
                    console.error('Polling error:', err);
                }
            }
        }, POLLING_INTERVAL);
    }

    // ------------------ INITIALIZE ------------------
    currentUserID = userSelect.value;
    updateUserData(currentUserID);
    startPolling();

    userSelect.addEventListener('change', () => {
        const userID = userSelect.value;
        updateUserData(userID);
    });
});
