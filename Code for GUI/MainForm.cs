using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // Main WinForms dashboard for monitoring JDY-31 telemetry and previewing the embedded OLED pages.
    public sealed class MainForm : Form
    {
        // Shared theme colors and fonts keep the different pages visually consistent.
        private readonly Color _pink = Color.FromArgb(247, 111, 218);
        private readonly Color _blue = Color.FromArgb(93, 196, 255);
        private readonly Color _navy = Color.FromArgb(38, 58, 102);
        private readonly Color _surface = Color.FromArgb(255, 255, 255);
        private readonly Color _softSurface = Color.FromArgb(252, 247, 255);
        private readonly Font _titleFont = new Font("Segoe UI Semibold", 18F, FontStyle.Bold, GraphicsUnit.Point);
        private readonly Font _bigValueFont = new Font("Segoe UI", 38F, FontStyle.Bold, GraphicsUnit.Point);
        private readonly Font _metricValueFont = new Font("Segoe UI Semibold", 22F, FontStyle.Bold, GraphicsUnit.Point);
        private readonly Font _metricTitleFont = new Font("Segoe UI", 10.5F, FontStyle.Regular, GraphicsUnit.Point);
        private readonly Font _smallFont = new Font("Segoe UI", 9.5F, FontStyle.Regular, GraphicsUnit.Point);

        // Top-bar controls for connection management and quick status feedback.
        private readonly ComboBox _portComboBox = new ComboBox();
        private readonly ComboBox _baudComboBox = new ComboBox();
        private readonly Button _refreshPortsButton = new Button();
        private readonly Button _connectButton = new Button();
        private readonly Button _demoButton = new Button();
        private readonly Label _connectionLabel = new Label();
        private readonly Label _packetLabel = new Label();
        private readonly Label _parserLabel = new Label();
        private readonly BufferedPanel _pageHost = new BufferedPanel();
        private readonly TextBox _rawLogBox = new TextBox();
        private readonly Timer _uiTimer = new Timer();
        private readonly Timer _demoTimer = new Timer();
        // Runtime helpers for serial buffering, demo-data generation, and page switching.
        private readonly StringBuilder _serialBuffer = new StringBuilder();
        private readonly object _serialSync = new object();
        private readonly Random _random = new Random();
        private readonly Dictionary<string, Control> _pages = new Dictionary<string, Control>();

        private SerialPort _serialPort;
        private TelemetryState _state = new TelemetryState();
        private bool _demoMode;
        private bool _updatingThresholds;

        // Cached references to page-specific widgets so UpdateUiFromState() can refresh them cheaply.
        private OledPreviewControl _homeOled;
        private Label _homeTimeLabel;
        private Label _homeDateLabel;
        private Label _homeTimeSourceLabel;
        private Label _homeConnectionValue;
        private Label _homeGpsValue;
        private Label _homeHeartValue;
        private Label _homeLightValue;

        private OledPreviewControl _tempOled;
        private Label _tempValueLabel;
        private Label _tempComfortValueLabel;
        private Label _humidityValueLabel;
        private Label _humidityComfortValueLabel;
        private Label _climateValueLabel;
        private Label _tempWarnValueLabel;
        private NumericUpDown _tempWarnNumeric;
        private Label _tempWarnSourceLabel;

        private OledPreviewControl _heartOled;
        private Label _hrValueLabel;
        private Label _spo2ValueLabel;
        private Label _fingerValueLabel;
        private Label _heartWarnValueLabel;
        private NumericUpDown _heartWarnNumeric;
        private Label _heartWarnSourceLabel;

        private OledPreviewControl _gpsOled;
        private Label _gpsSourceValueLabel;
        private Label _gpsFixValueLabel;
        private Label _gpsLatValueLabel;
        private Label _gpsLonValueLabel;
        private Label _gpsFreshValueLabel;

        private OledPreviewControl _lightOled;
        private Label _adcValueLabel;
        private Label _lightPercentValueLabel;
        private Label _lightLevelValueLabel;
        private Label _lightExtraNoteLabel;

        private OledPreviewControl _motionOled;
        private Label _stepValueLabel;
        private Label _rollValueLabel;
        private Label _pitchValueLabel;
        private Label _yawValueLabel;
        private Label _motionExtraNoteLabel;
        private Label _motionGyroValueLabel;

        public MainForm()
        {
            // Build the shell once, then drive the content from telemetry packets and timers.
            InitializeWindow();
            BuildLayout();
            RefreshPorts();
            UpdateUiFromState();

            _uiTimer.Interval = 400;
            _uiTimer.Tick += UiTimer_Tick;
            _uiTimer.Start();

            _demoTimer.Interval = 1000;
            _demoTimer.Tick += DemoTimer_Tick;
        }

        // Configure the base form before any child controls are created.
        private void InitializeWindow()
        {
            Text = "JDY-31 OLED Desktop Monitor";
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(1380, 860);
            ClientSize = new Size(1540, 920);
            BackColor = Color.White;
            Font = new Font("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
            AutoScaleMode = AutoScaleMode.None;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            UpdateStyles();
        }

        // Compose the window into three bands: toolbar, page content, and raw telemetry log.
        private void BuildLayout()
        {
            Image background = TryLoadBackgroundTexture();

            GradientPanel root = new GradientPanel
            {
                Dock = DockStyle.Fill,
                BackgroundTexture = background,
                BackgroundTextureLayout = ImageLayout.Stretch,
                ColorA = Color.FromArgb(170, 255, 255, 255),
                ColorB = Color.FromArgb(120, 255, 255, 255),
                Padding = new Padding(18, 18, 18, 18)
            };
            Controls.Add(root);

            Panel topBar = BuildTopBar();
            topBar.Dock = DockStyle.Top;
            root.Controls.Add(topBar);

            Panel logPanel = BuildLogPanel();
            logPanel.Dock = DockStyle.Bottom;
            root.Controls.Add(logPanel);

            RoundedPanel centerShell = CreateRoundedPanel();
            centerShell.Dock = DockStyle.Fill;
            centerShell.Padding = new Padding(18, 16, 18, 16);
            centerShell.FillColor = Color.FromArgb(210, 255, 255, 255);
            centerShell.BorderColor = Color.FromArgb(210, 220, 232, 246);
            root.Controls.Add(centerShell);

            _pageHost.Dock = DockStyle.Fill;
            _pageHost.BackColor = Color.Transparent;
            _pageHost.AutoScroll = false;
            centerShell.Controls.Add(_pageHost);

            BuildPages();
            ShowPage("home");
        }

        private Panel BuildTopBar()
        {
            RoundedPanel panel = CreateRoundedPanel();
            panel.Height = 108;
            panel.Padding = new Padding(22, 16, 22, 14);
            panel.FillColor = Color.FromArgb(235, 255, 255, 255);

            Label title = new Label
            {
                AutoSize = true,
                Text = "JDY-31 Bluetooth OLED Desktop Monitor",
                Font = new Font("Segoe UI Semibold", 22F, FontStyle.Bold, GraphicsUnit.Point),
                ForeColor = _navy,
                Location = new Point(24, 16),
                BackColor = Color.Transparent
            };
            panel.Controls.Add(title);

            BufferedFlowLayoutPanel actions = new BufferedFlowLayoutPanel
            {
                FlowDirection = FlowDirection.LeftToRight,
                AutoSize = true,
                WrapContents = false,
                Anchor = AnchorStyles.Top | AnchorStyles.Right,
                Location = new Point(760, 16),
                BackColor = Color.Transparent,
                Margin = new Padding(0)
            };
            panel.Controls.Add(actions);
            panel.Resize += delegate
            {
                actions.Location = new Point(Math.Max(22, panel.ClientSize.Width - actions.Width - 22), 16);
            };

            _portComboBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _portComboBox.Width = 126;
            _portComboBox.Margin = new Padding(6);
            actions.Controls.Add(WrapLabeledControl("COM", _portComboBox));

            _baudComboBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _baudComboBox.Width = 96;
            _baudComboBox.Items.AddRange(new object[] { "9600", "115200" });
            _baudComboBox.SelectedIndex = 0;
            actions.Controls.Add(WrapLabeledControl("Baud", _baudComboBox));

            StyleButton(_refreshPortsButton, "Refresh Ports", _blue, new Size(122, 42));
            _refreshPortsButton.Click += delegate { RefreshPorts(); };
            actions.Controls.Add(_refreshPortsButton);

            StyleButton(_connectButton, "Connect JDY-31", _pink, new Size(150, 42));
            _connectButton.Click += ConnectButton_Click;
            actions.Controls.Add(_connectButton);

            StyleButton(_demoButton, "Demo Mode", Color.FromArgb(155, 124, 255), new Size(122, 42));
            _demoButton.Click += DemoButton_Click;
            actions.Controls.Add(_demoButton);

            _connectionLabel.AutoSize = true;
            _connectionLabel.Font = new Font("Segoe UI Semibold", 10.5F, FontStyle.Bold, GraphicsUnit.Point);
            _connectionLabel.ForeColor = Color.FromArgb(73, 98, 125);
            _connectionLabel.Location = new Point(26, 70);
            _connectionLabel.BackColor = Color.Transparent;
            panel.Controls.Add(_connectionLabel);

            _packetLabel.AutoSize = true;
            _packetLabel.Font = _smallFont;
            _packetLabel.ForeColor = Color.FromArgb(108, 123, 152);
            _packetLabel.Location = new Point(274, 71);
            _packetLabel.BackColor = Color.Transparent;
            panel.Controls.Add(_packetLabel);

            return panel;
        }

        private Panel BuildLogPanel()
        {
            RoundedPanel panel = CreateRoundedPanel();
            panel.Height = 146;
            panel.Padding = new Padding(16, 12, 16, 12);
            panel.FillColor = Color.FromArgb(232, 255, 255, 255);

            Label label = new Label
            {
                AutoSize = true,
                Text = "JDY-31 Raw Telemetry / Debug Log",
                Font = new Font("Segoe UI Semibold", 11F, FontStyle.Bold, GraphicsUnit.Point),
                ForeColor = _navy,
                Location = new Point(16, 10),
                BackColor = Color.Transparent
            };
            panel.Controls.Add(label);

            _rawLogBox.Multiline = true;
            _rawLogBox.ReadOnly = true;
            _rawLogBox.ScrollBars = ScrollBars.Vertical;
            _rawLogBox.Dock = DockStyle.Fill;
            _rawLogBox.BorderStyle = BorderStyle.None;
            _rawLogBox.WordWrap = false;
            _rawLogBox.BackColor = Color.FromArgb(248, 250, 255);
            _rawLogBox.ForeColor = Color.FromArgb(70, 76, 98);
            _rawLogBox.Font = new Font("Consolas", 9F, FontStyle.Regular, GraphicsUnit.Point);
            _rawLogBox.Margin = new Padding(0);

            BufferedPanel host = new BufferedPanel
            {
                Dock = DockStyle.Fill,
                Padding = new Padding(0, 28, 0, 0),
                BackColor = Color.Transparent
            };
            host.Controls.Add(_rawLogBox);
            panel.Controls.Add(host);
            label.BringToFront();

            return panel;
        }

        // Create every logical page up front and then show/hide them instead of rebuilding controls on navigation.
        private void BuildPages()
        {
            Control home = BuildHomePage();
            Control temp = BuildTempPage();
            Control heart = BuildHeartPage();
            Control gps = BuildGpsPage();
            Control light = BuildLightPage();
            Control motion = BuildMotionPage();

            _pages["home"] = home;
            _pages["temp"] = temp;
            _pages["heart"] = heart;
            _pages["gps"] = gps;
            _pages["light"] = light;
            _pages["motion"] = motion;

            foreach (Control page in _pages.Values)
            {
                page.Dock = DockStyle.Fill;
                page.Visible = false;
                _pageHost.Controls.Add(page);
            }
        }

        private Control BuildHomePage()
        {
            GradientPanel page = CreatePagePanel();
            page.Padding = new Padding(28);

            TableLayoutPanel layout = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 2,
                RowCount = 1,
                BackColor = Color.Transparent
            };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 56F));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 44F));
            page.Controls.Add(layout);

            TableLayoutPanel left = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 5,
                BackColor = Color.Transparent,
                Padding = new Padding(0)
            };
            left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            left.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            layout.Controls.Add(left, 0, 0);

            Label heading = new Label
            {
                AutoSize = true,
                Text = "Home / RTC",
                Font = _titleFont,
                ForeColor = _navy,
                Margin = new Padding(8, 8, 8, 0),
                BackColor = Color.Transparent
            };
            left.Controls.Add(heading, 0, 0);

            Label subHeading = new Label
            {
                AutoSize = true,
                MaximumSize = new Size(760, 0),
                Text = "The default screen shows time directly. Use the five buttons below to open the other OLED pages.",
                Font = _smallFont,
                ForeColor = Color.FromArgb(112, 126, 150),
                Margin = new Padding(10, 8, 12, 0),
                BackColor = Color.Transparent
            };
            left.Controls.Add(subHeading, 0, 1);

            _homeTimeLabel = new Label
            {
                AutoSize = true,
                Text = "--:--:--",
                Font = _bigValueFont,
                ForeColor = _navy,
                Margin = new Padding(8, 26, 8, 0),
                BackColor = Color.Transparent
            };
            left.Controls.Add(_homeTimeLabel, 0, 2);

            BufferedFlowLayoutPanel timeMeta = new BufferedFlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoSize = true,
                Margin = new Padding(10, 4, 10, 0),
                BackColor = Color.Transparent
            };
            _homeDateLabel = new Label
            {
                AutoSize = true,
                Text = "Waiting for JDY-31 time data",
                Font = new Font("Segoe UI", 14F, FontStyle.Regular, GraphicsUnit.Point),
                ForeColor = Color.FromArgb(90, 108, 138),
                BackColor = Color.Transparent,
                Margin = new Padding(0, 0, 0, 2)
            };
            timeMeta.Controls.Add(_homeDateLabel);

            _homeTimeSourceLabel = new Label
            {
                AutoSize = true,
                Text = "Time Source: --",
                Font = _smallFont,
                ForeColor = Color.FromArgb(106, 122, 152),
                BackColor = Color.Transparent,
                Margin = new Padding(0, 0, 0, 0)
            };
            timeMeta.Controls.Add(_homeTimeSourceLabel);
            left.Controls.Add(timeMeta, 0, 3);

            BufferedFlowLayoutPanel quickPanel = new BufferedFlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                BackColor = Color.Transparent,
                WrapContents = true,
                AutoScroll = true,
                FlowDirection = FlowDirection.LeftToRight,
                Padding = new Padding(0, 28, 0, 0),
                Margin = new Padding(0)
            };
            left.Controls.Add(quickPanel, 0, 4);

            quickPanel.Controls.Add(CreateMetricCard("Connection", _blue, out _homeConnectionValue, null));
            quickPanel.Controls.Add(CreateMetricCard("Position", _pink, out _homeGpsValue, null));
            quickPanel.Controls.Add(CreateMetricCard("Heart Rate / SpO2", Color.FromArgb(155, 124, 255), out _homeHeartValue, null));
            quickPanel.Controls.Add(CreateMetricCard("Light / Steps", Color.FromArgb(95, 190, 173), out _homeLightValue, null));

            TableLayoutPanel right = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 3,
                BackColor = Color.Transparent,
                Padding = new Padding(0)
            };
            right.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            right.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            right.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            layout.Controls.Add(right, 1, 0);

            Label rightTitle = new Label
            {
                AutoSize = true,
                Text = "Navigation",
                Font = new Font("Segoe UI Semibold", 15F, FontStyle.Bold, GraphicsUnit.Point),
                ForeColor = _navy,
                Margin = new Padding(10, 12, 10, 0),
                BackColor = Color.Transparent
            };
            right.Controls.Add(rightTitle, 0, 0);

            BufferedFlowLayoutPanel nav = new BufferedFlowLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Color.Transparent,
                WrapContents = true,
                FlowDirection = FlowDirection.LeftToRight,
                Margin = new Padding(0, 12, 0, 0),
                Padding = new Padding(0, 0, 0, 0)
            };
            right.Controls.Add(nav, 0, 1);

            nav.Controls.Add(CreateNavButton("Temp / Humi", _pink, delegate { ShowPage("temp"); }));
            nav.Controls.Add(CreateNavButton("Heart Rate / SpO2", _blue, delegate { ShowPage("heart"); }));
            nav.Controls.Add(CreateNavButton("GPS / Bluetooth", Color.FromArgb(146, 122, 255), delegate { ShowPage("gps"); }));
            nav.Controls.Add(CreateNavButton("Light", Color.FromArgb(96, 190, 172), delegate { ShowPage("light"); }));
            nav.Controls.Add(CreateNavButton("Motion", Color.FromArgb(110, 155, 255), delegate { ShowPage("motion"); }));

            _homeOled = new OledPreviewControl
            {
                Size = new Size(340, 246)
            };
            Panel homePreview = WrapPreviewPanel(_homeOled, "Firmware OLED: RTC");
            homePreview.Dock = DockStyle.Fill;
            homePreview.Margin = new Padding(0, 16, 0, 0);
            right.Controls.Add(homePreview, 0, 2);

            return page;
        }

        private Control BuildTempPage()
        {
            GradientPanel page = CreatePagePanel();
            page.Controls.Add(CreatePageHeader("Temperature & Humidity / OLED Page 1", delegate { ShowPage("home"); }));

            TableLayoutPanel body = CreateTwoColumnBody();
            page.Controls.Add(body);

            _tempOled = new OledPreviewControl();
            body.Controls.Add(WrapPreviewPanel(_tempOled, "Firmware OLED: Temp / Humi"), 0, 0);

            FlowLayoutPanel info = CreateInfoFlow();
            body.Controls.Add(info, 1, 0);

            info.Controls.Add(CreateMetricCard("Temperature", _pink, out _tempValueLabel, "Unit: °C"));
            info.Controls.Add(CreateMetricCard("Temp Status", _blue, out _tempComfortValueLabel, "Matches OLED line 4"));
            info.Controls.Add(CreateMetricCard("Humidity", Color.FromArgb(100, 191, 173), out _humidityValueLabel, "Unit: %"));
            info.Controls.Add(CreateMetricCard("Humidity Status", Color.FromArgb(155, 124, 255), out _humidityComfortValueLabel, "Matches OLED line 7"));
            info.Controls.Add(CreateMetricCard("Comfort Summary", Color.FromArgb(255, 158, 204), out _climateValueLabel, "Recalculated locally from the source rules"));
            info.Controls.Add(CreateMetricCard("Temp Warning Threshold", Color.FromArgb(110, 155, 255), out _tempWarnValueLabel, "Default: 30°C"));

            Panel settingsCard = CreateWideNoteCard("Temperature Threshold", _pink, out _tempWarnSourceLabel);
            BufferedFlowLayoutPanel settingInner = new BufferedFlowLayoutPanel
            {
                Dock = DockStyle.Bottom,
                Height = 48,
                BackColor = Color.Transparent,
                FlowDirection = FlowDirection.LeftToRight,
                WrapContents = false,
                Padding = new Padding(12, 0, 0, 10)
            };
            _tempWarnNumeric = new NumericUpDown
            {
                Width = 100,
                Minimum = 0,
                Maximum = 60,
                Value = 30,
                Font = new Font("Segoe UI Semibold", 11F, FontStyle.Bold, GraphicsUnit.Point)
            };
            _tempWarnNumeric.ValueChanged += TempWarnNumeric_ValueChanged;
            settingInner.Controls.Add(new Label
            {
                AutoSize = true,
                Text = "GUI Threshold:",
                Font = _smallFont,
                ForeColor = _navy,
                Margin = new Padding(0, 12, 8, 0)
            });
            settingInner.Controls.Add(_tempWarnNumeric);
            settingsCard.Controls.Add(settingInner);
            info.Controls.Add(settingsCard);

            return page;
        }

        private Control BuildHeartPage()
        {
            GradientPanel page = CreatePagePanel();
            page.Controls.Add(CreatePageHeader("Heart Rate & SpO2 / OLED Page 2", delegate { ShowPage("home"); }));

            TableLayoutPanel body = CreateTwoColumnBody();
            page.Controls.Add(body);

            _heartOled = new OledPreviewControl();
            body.Controls.Add(WrapPreviewPanel(_heartOled, "Firmware OLED: Heart Rate & SPO2"), 0, 0);

            FlowLayoutPanel info = CreateInfoFlow();
            body.Controls.Add(info, 1, 0);

            info.Controls.Add(CreateMetricCard("Heart Rate", _pink, out _hrValueLabel, "Unit: bpm"));
            info.Controls.Add(CreateMetricCard("SpO2", _blue, out _spo2ValueLabel, "Unit: %"));
            info.Controls.Add(CreateMetricCard("Finger Detection", Color.FromArgb(100, 191, 173), out _fingerValueLabel, "Matches the last OLED line"));
            info.Controls.Add(CreateMetricCard("Heart Rate Warning Threshold", Color.FromArgb(146, 122, 255), out _heartWarnValueLabel, "Default: 120 bpm"));

            Panel settingsCard = CreateWideNoteCard("Heart Rate Threshold", _blue, out _heartWarnSourceLabel);
            BufferedFlowLayoutPanel settingInner = new BufferedFlowLayoutPanel
            {
                Dock = DockStyle.Bottom,
                Height = 48,
                BackColor = Color.Transparent,
                FlowDirection = FlowDirection.LeftToRight,
                WrapContents = false,
                Padding = new Padding(12, 0, 0, 10)
            };
            _heartWarnNumeric = new NumericUpDown
            {
                Width = 100,
                Minimum = 60,
                Maximum = 200,
                Value = 120,
                Font = new Font("Segoe UI Semibold", 11F, FontStyle.Bold, GraphicsUnit.Point)
            };
            _heartWarnNumeric.ValueChanged += HeartWarnNumeric_ValueChanged;
            settingInner.Controls.Add(new Label
            {
                AutoSize = true,
                Text = "GUI Threshold:",
                Font = _smallFont,
                ForeColor = _navy,
                Margin = new Padding(0, 12, 8, 0)
            });
            settingInner.Controls.Add(_heartWarnNumeric);
            settingsCard.Controls.Add(settingInner);
            info.Controls.Add(settingsCard);

            return page;
        }

        private Control BuildGpsPage()
        {
            GradientPanel page = CreatePagePanel();
            page.Controls.Add(CreatePageHeader("GPS / Bluetooth / OLED Page 3", delegate { ShowPage("home"); }));

            TableLayoutPanel body = CreateTwoColumnBody();
            page.Controls.Add(body);

            _gpsOled = new OledPreviewControl();
            body.Controls.Add(WrapPreviewPanel(_gpsOled, "Firmware OLED: GPS / BT"), 0, 0);

            FlowLayoutPanel info = CreateInfoFlow();
            body.Controls.Add(info, 1, 0);

            info.Controls.Add(CreateMetricCard("Position Source", _pink, out _gpsSourceValueLabel, "GNSS / BT / NONE"));
            info.Controls.Add(CreateMetricCard("Fix Status", _blue, out _gpsFixValueLabel, "A = valid, V = invalid"));
            info.Controls.Add(CreateMetricCard("Latitude", Color.FromArgb(100, 191, 173), out _gpsLatValueLabel, "Supports trailing lat,lon format"));
            info.Controls.Add(CreateMetricCard("Longitude", Color.FromArgb(146, 122, 255), out _gpsLonValueLabel, "Supports the current firmware format"));
            info.Controls.Add(CreateWideNoteCard("Position Freshness", Color.FromArgb(110, 155, 255), out _gpsFreshValueLabel));

            return page;
        }

        private Control BuildLightPage()
        {
            GradientPanel page = CreatePagePanel();
            page.Controls.Add(CreatePageHeader("Light Sensor / OLED Page 4", delegate { ShowPage("home"); }));

            TableLayoutPanel body = CreateTwoColumnBody();
            page.Controls.Add(body);

            _lightOled = new OledPreviewControl();
            body.Controls.Add(WrapPreviewPanel(_lightOled, "Firmware OLED: Light Sensor"), 0, 0);

            FlowLayoutPanel info = CreateInfoFlow();
            body.Controls.Add(info, 1, 0);

            info.Controls.Add(CreateMetricCard("ADC Raw", _pink, out _adcValueLabel, "OLED line 3"));
            info.Controls.Add(CreateMetricCard("Light Percentage", _blue, out _lightPercentValueLabel, "LIGHT field"));
            info.Controls.Add(CreateMetricCard("Light Level", Color.FromArgb(100, 191, 173), out _lightLevelValueLabel, "Bright / Normal / Dark"));
            info.Controls.Add(CreateWideNoteCard("Telemetry Status", Color.FromArgb(146, 122, 255), out _lightExtraNoteLabel));

            return page;
        }

        private Control BuildMotionPage()
        {
            GradientPanel page = CreatePagePanel();
            page.Controls.Add(CreatePageHeader("Motion / OLED Page 5", delegate { ShowPage("home"); }));

            TableLayoutPanel body = CreateTwoColumnBody();
            page.Controls.Add(body);

            _motionOled = new OledPreviewControl();
            body.Controls.Add(WrapPreviewPanel(_motionOled, "Firmware OLED: Motion"), 0, 0);

            FlowLayoutPanel info = CreateInfoFlow();
            body.Controls.Add(info, 1, 0);

            info.Controls.Add(CreateMetricCard("Step Count", _pink, out _stepValueLabel, "OLED line 3"));
            info.Controls.Add(CreateMetricCard("Roll", _blue, out _rollValueLabel, "OLED line 5"));
            info.Controls.Add(CreateMetricCard("Pitch", Color.FromArgb(100, 191, 173), out _pitchValueLabel, "OLED line 6"));
            info.Controls.Add(CreateMetricCard("Yaw", Color.FromArgb(146, 122, 255), out _yawValueLabel, "OLED line 7"));
            info.Controls.Add(CreateWideNoteCard("Acceleration (AX / AY / AZ)", Color.FromArgb(110, 155, 255), out _motionExtraNoteLabel));
            info.Controls.Add(CreateWideNoteCard("Gyroscope (GX / GY / GZ)", Color.FromArgb(255, 158, 204), out _motionGyroValueLabel));

            return page;
        }

        private GradientPanel CreatePagePanel()
        {
            return new GradientPanel
            {
                BackColor = Color.Transparent,
                BackgroundTexture = TryLoadBackgroundTexture(),
                BackgroundTextureLayout = ImageLayout.Stretch,
                ColorA = Color.FromArgb(210, 255, 255, 255),
                ColorB = Color.FromArgb(188, 250, 252, 255),
                GradientMode = System.Drawing.Drawing2D.LinearGradientMode.Vertical,
                Padding = new Padding(24, 90, 24, 24),
                AutoScroll = true
            };
        }

        private Panel CreatePageHeader(string title, Action backAction)
        {
            Panel header = new Panel
            {
                Dock = DockStyle.Top,
                Height = 64,
                BackColor = Color.Transparent
            };

            Button backButton = new Button
            {
                Text = "Back to Home",
                Width = 132,
                Height = 42,
                FlatStyle = FlatStyle.Flat,
                BackColor = Color.FromArgb(240, 248, 255),
                ForeColor = _navy,
                Font = new Font("Segoe UI Semibold", 10F, FontStyle.Bold, GraphicsUnit.Point),
                Location = new Point(0, 8),
                Cursor = Cursors.Hand
            };
            backButton.FlatAppearance.BorderColor = Color.FromArgb(205, 220, 240);
            backButton.FlatAppearance.BorderSize = 1;
            backButton.Click += delegate { backAction(); };
            header.Controls.Add(backButton);

            Label titleLabel = new Label
            {
                AutoSize = true,
                Text = title,
                Font = _titleFont,
                ForeColor = _navy,
                Location = new Point(152, 8)
            };
            header.Controls.Add(titleLabel);

            Label tip = new Label
            {
                AutoSize = true,
                Text = "OLED mirror on the left, enhanced desktop view on the right",
                Font = _smallFont,
                ForeColor = Color.FromArgb(110, 122, 150),
                Location = new Point(156, 38)
            };
            header.Controls.Add(tip);

            return header;
        }

        private TableLayoutPanel CreateTwoColumnBody()
        {
            TableLayoutPanel body = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 2,
                RowCount = 1,
                BackColor = Color.Transparent,
                Padding = new Padding(0, 10, 0, 0)
            };
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 400F));
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            return body;
        }

        private Panel WrapPreviewPanel(Control preview, string caption)
        {
            RoundedPanel panel = CreateRoundedPanel();
            panel.Dock = DockStyle.Fill;
            panel.Padding = new Padding(18);

            Label title = new Label
            {
                AutoSize = true,
                Text = caption,
                Font = new Font("Segoe UI Semibold", 11F, FontStyle.Bold, GraphicsUnit.Point),
                ForeColor = _navy,
                Location = new Point(18, 14),
                BackColor = Color.Transparent
            };
            panel.Controls.Add(title);

            preview.Size = new Size(320, 240);
            preview.Anchor = AnchorStyles.Top;
            panel.Controls.Add(preview);

            Action updatePreviewBounds = delegate
            {
                int top = title.Bottom + 18;
                preview.Left = Math.Max(18, (panel.ClientSize.Width - preview.Width) / 2);
                preview.Top = top;
            };

            panel.Resize += delegate { updatePreviewBounds(); };
            updatePreviewBounds();

            return panel;
        }

        private FlowLayoutPanel CreateInfoFlow()
        {
            return new BufferedFlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                BackColor = Color.Transparent,
                FlowDirection = FlowDirection.LeftToRight,
                WrapContents = true,
                AutoScroll = true,
                Padding = new Padding(16, 2, 8, 8)
            };
        }

        private Panel CreateMetricCard(string title, Color accent, out Label valueLabel, string subtitle)
        {
            Panel card = CreateRoundedPanel();
            card.Width = 290;
            card.Height = 146;
            card.Margin = new Padding(12);
            card.Padding = new Padding(18, 16, 18, 16);

            Panel accentBar = new Panel
            {
                Width = 8,
                Height = 54,
                BackColor = accent,
                Location = new Point(0, 18)
            };
            card.Controls.Add(accentBar);

            Label titleLabel = new Label
            {
                AutoSize = false,
                Text = title,
                Font = _metricTitleFont,
                ForeColor = Color.FromArgb(110, 124, 150),
                Location = new Point(22, 14),
                Size = new Size(246, 24)
            };
            card.Controls.Add(titleLabel);

            valueLabel = new Label
            {
                AutoSize = false,
                Text = "--",
                Font = _metricValueFont,
                ForeColor = _navy,
                Location = new Point(22, 44),
                Size = new Size(246, 48)
            };
            card.Controls.Add(valueLabel);

            if (!string.IsNullOrEmpty(subtitle))
            {
                Label subLabel = new Label
                {
                    AutoSize = false,
                    Text = subtitle,
                    Font = _smallFont,
                    ForeColor = Color.FromArgb(118, 129, 152),
                    Location = new Point(22, 102),
                    Size = new Size(246, 28)
                };
                card.Controls.Add(subLabel);
            }

            return card;
        }

        private Panel CreateWideNoteCard(string title, Color accent, out Label noteLabel)
        {
            Panel card = CreateRoundedPanel();
            card.Width = 620;
            card.Height = 138;
            card.Margin = new Padding(12);
            card.Padding = new Padding(18, 16, 18, 16);

            Panel accentBar = new Panel
            {
                Width = 8,
                Height = 60,
                BackColor = accent,
                Location = new Point(0, 18)
            };
            card.Controls.Add(accentBar);

            Label titleLabel = new Label
            {
                AutoSize = false,
                Text = title,
                Font = _metricTitleFont,
                ForeColor = Color.FromArgb(110, 124, 150),
                Location = new Point(22, 14),
                Size = new Size(580, 24)
            };
            card.Controls.Add(titleLabel);

            noteLabel = new Label
            {
                AutoSize = false,
                Text = "--",
                Font = new Font("Segoe UI", 10.5F, FontStyle.Regular, GraphicsUnit.Point),
                ForeColor = _navy,
                Location = new Point(22, 44),
                Size = new Size(580, 60)
            };
            card.Controls.Add(noteLabel);

            return card;
        }

        private RoundedPanel CreateRoundedPanel()
        {
            return new RoundedPanel
            {
                FillColor = Color.FromArgb(236, 255, 255, 255),
                BorderColor = Color.FromArgb(198, 214, 228, 246),
                CornerRadius = 26,
                Margin = new Padding(0)
            };
        }

        private Control WrapLabeledControl(string text, Control child)
        {
            BufferedFlowLayoutPanel panel = new BufferedFlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoSize = true,
                Margin = new Padding(6, 0, 6, 0),
                BackColor = Color.Transparent
            };
            panel.Controls.Add(new Label
            {
                AutoSize = true,
                Text = text,
                Font = new Font("Segoe UI", 8.5F, FontStyle.Regular, GraphicsUnit.Point),
                ForeColor = Color.FromArgb(114, 128, 150)
            });
            panel.Controls.Add(child);
            return panel;
        }

        private Button CreateNavButton(string text, Color color, Action action)
        {
            Button button = new Button
            {
                Text = text,
                Width = 210,
                Height = 84,
                FlatStyle = FlatStyle.Flat,
                BackColor = color,
                ForeColor = Color.White,
                Font = new Font("Segoe UI Semibold", 12.5F, FontStyle.Bold, GraphicsUnit.Point),
                Margin = new Padding(12),
                Cursor = Cursors.Hand
            };
            button.FlatAppearance.BorderSize = 0;
            button.Click += delegate { action(); };
            return button;
        }

        private void StyleButton(Button button, string text, Color color, Size size)
        {
            button.Text = text;
            button.Size = size;
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 0;
            button.BackColor = color;
            button.ForeColor = Color.White;
            button.Font = new Font("Segoe UI Semibold", 10.5F, FontStyle.Bold, GraphicsUnit.Point);
            button.Margin = new Padding(8, 18, 8, 0);
            button.Cursor = Cursors.Hand;
        }

        private static Image TryLoadBackgroundTexture()
        {
            try
            {
                string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Assets", "ui_background.png");
                if (!File.Exists(path))
                {
                    return null;
                }

                using (Image source = Image.FromFile(path))
                {
                    return new Bitmap(source);
                }
            }
            catch
            {
                return null;
            }
        }

        // Only one detail page is visible at a time, mirroring the single-page OLED navigation model.
        private void ShowPage(string key)
        {
            foreach (KeyValuePair<string, Control> page in _pages)
            {
                page.Value.Visible = string.Equals(page.Key, key, StringComparison.OrdinalIgnoreCase);
            }
        }

        // Refresh the COM-port list so the operator can reconnect without restarting the desktop tool.
        private void RefreshPorts()
        {
            string[] ports = SerialPort.GetPortNames();
            Array.Sort(ports, StringComparer.OrdinalIgnoreCase);
            _portComboBox.Items.Clear();
            _portComboBox.Items.AddRange(ports);
            if (_portComboBox.Items.Count > 0)
            {
                _portComboBox.SelectedIndex = 0;
            }
        }

        // Toggle the serial link used to receive telemetry from the JDY-31 Bluetooth bridge.
        private void ConnectButton_Click(object sender, EventArgs e)
        {
            if (_serialPort != null && _serialPort.IsOpen)
            {
                DisconnectSerial();
                return;
            }

            if (_demoMode)
            {
                StopDemoMode();
            }

            if (_portComboBox.SelectedItem == null)
            {
                MessageBox.Show(this, "Please select a COM port first.", "JDY-31", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            try
            {
                _serialPort = new SerialPort(_portComboBox.SelectedItem.ToString(), int.Parse(_baudComboBox.SelectedItem.ToString()));
                _serialPort.NewLine = "\n";
                _serialPort.DtrEnable = false;
                _serialPort.RtsEnable = false;
                _serialPort.ReadTimeout = 500;
                _serialPort.DataReceived += SerialPort_DataReceived;
                _serialPort.Open();
                _connectButton.Text = "Disconnect";
                AppendLog("[Serial] Connected to " + _serialPort.PortName + " @ " + _serialPort.BaudRate);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Failed to open the serial port: " + ex.Message, "JDY-31", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }

            UpdateUiFromState();
        }

        // Centralized disconnect path shared by manual disconnects, demo mode, and form shutdown.
        private void DisconnectSerial()
        {
            try
            {
                if (_serialPort != null)
                {
                    _serialPort.DataReceived -= SerialPort_DataReceived;
                    if (_serialPort.IsOpen)
                    {
                        _serialPort.Close();
                    }
                    _serialPort.Dispose();
                    _serialPort = null;
                    AppendLog("[Serial] Disconnected.");
                }
            }
            catch (Exception ex)
            {
                AppendLog("[Serial] Disconnect error: " + ex.Message);
            }

            _connectButton.Text = "Connect JDY-31";
            UpdateUiFromState();
        }

        // Demo mode feeds synthetic packets through the same parser/UI path as live data.
        private void DemoButton_Click(object sender, EventArgs e)
        {
            if (_demoMode)
            {
                StopDemoMode();
                return;
            }

            if (_serialPort != null && _serialPort.IsOpen)
            {
                DisconnectSerial();
            }

            _demoMode = true;
            _demoButton.Text = "Stop Demo";
            _demoTimer.Start();
            AppendLog("[Demo] Demo mode started.");
            ApplyTelemetryLine(GenerateDemoLine());
            UpdateUiFromState();
        }

        // Stop the timer and restore the UI to the normal disconnected state.
        private void StopDemoMode()
        {
            _demoMode = false;
            _demoTimer.Stop();
            _demoButton.Text = "Demo Mode";
            AppendLog("[Demo] Demo mode stopped.");
            UpdateUiFromState();
        }

        // SerialPort raises this callback on a worker thread, so complete lines are staged and then marshalled onto the UI thread.
        private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                if (_serialPort == null || !_serialPort.IsOpen)
                {
                    return;
                }

                string chunk = _serialPort.ReadExisting();
                if (string.IsNullOrEmpty(chunk))
                {
                    return;
                }

                List<string> lines = new List<string>();
                lock (_serialSync)
                {
                    _serialBuffer.Append(chunk);
                    string buffer = _serialBuffer.ToString();
                    int lineIndex;
                    while ((lineIndex = buffer.IndexOf('\n')) >= 0)
                    {
                        string line = buffer.Substring(0, lineIndex).Trim('\r', '\n', ' ');
                        if (line.Length > 0)
                        {
                            lines.Add(line);
                        }
                        buffer = buffer.Substring(lineIndex + 1);
                    }
                    _serialBuffer.Clear();
                    _serialBuffer.Append(buffer);
                }

                foreach (string line in lines)
                {
                    // WinForms controls are not thread-safe, so parsing and UI updates happen via BeginInvoke().
                    BeginInvoke(new Action<string>(ApplyTelemetryLine), line);
                }
            }
            catch (Exception ex)
            {
                BeginInvoke(new Action(delegate { AppendLog("[Serial] Read error: " + ex.Message); }));
            }
        }

        private void DemoTimer_Tick(object sender, EventArgs e)
        {
            ApplyTelemetryLine(GenerateDemoLine());
        }

        // Produce a realistic packet with the same key names as the embedded firmware output.
        private string GenerateDemoLine()
        {
            DateTime now = DateTime.Now;
            int temp = 24 + _random.Next(-2, 4);
            int humi = 50 + _random.Next(-12, 14);
            int hr = 78 + _random.Next(-6, 7);
            int spo2 = 97 + _random.Next(-1, 2);
            long steps = (_state.StepCount ?? 1200) + _random.Next(1, 6);
            int lightPercent = 55 + _random.Next(-20, 21);
            int adcRaw = 1800 + _random.Next(-250, 260);
            int roll = _random.Next(-30, 31);
            int pitch = _random.Next(-25, 26);
            int yaw = _random.Next(-180, 181);
            int ax = _random.Next(-16000, 16001);
            int ay = _random.Next(-16000, 16001);
            int az = _random.Next(-16000, 16001);
            int gx = _random.Next(-250, 251);
            int gy = _random.Next(-250, 251);
            int gz = _random.Next(-250, 251);
            string source = _random.Next(0, 4) == 0 ? "BT" : "GNSS";
            string lat = source == "GNSS" ? "53.76332" : "53.76291";
            string lon = source == "GNSS" ? "-2.70441" : "-2.70386";

            return string.Format(
                "TIME={0:yyyy-MM-dd HH:mm:ss},TEMP={1},HUMI={2},COMFORT=Comfort,HR={3},SPO2={4},FINGER=ON,STEP={5},LIGHT={6},ADC_RAW={7},ROLL={8},PITCH={9},YAW={10},AX={11},AY={12},AZ={13},GX={14},GY={15},GZ={16},TEMP_WARN=30,HR_WARN=120,POSSRC={17},POSFIX=A,{18},{19}",
                now,
                temp,
                humi,
                hr,
                spo2,
                steps,
                lightPercent,
                adcRaw,
                roll,
                pitch,
                yaw,
                ax,
                ay,
                az,
                gx,
                gy,
                gz,
                source,
                lat,
                lon);
        }

        // Parse one telemetry sentence, merge it into the current state, and immediately refresh the dashboard.
        private void ApplyTelemetryLine(string line)
        {
            TelemetryState next;
            string error;
            if (TelemetryParser.TryParseLine(line, _state, out next, out error))
            {
                // If the packet does not include threshold fields, preserve the values currently chosen in the desktop UI.
                if (!next.TemperatureWarn.HasValue)
                {
                    next.TemperatureWarn = (int)_tempWarnNumeric.Value;
                }
                if (!next.HeartRateWarn.HasValue)
                {
                    next.HeartRateWarn = (int)_heartWarnNumeric.Value;
                }

                _state = next;
                AppendLog(line);
                UpdateUiFromState();
            }
            else
            {
                AppendLog("[Parser] " + error + " >> " + line);
            }
        }

        // Periodic repaint keeps freshness labels and stale-data hints up to date even when no new packet arrives.
        private void UiTimer_Tick(object sender, EventArgs e)
        {
            UpdateUiFromState();
        }

        // Single fan-out point that repaints every view from the same telemetry snapshot.
        private void UpdateUiFromState()
        {
            UpdateTopStatus();
            UpdateHomePage();
            UpdateTempPage();
            UpdateHeartPage();
            UpdateGpsPage();
            UpdateLightPage();
            UpdateMotionPage();
        }

        // Surface connection state and packet count in the toolbar.
        private void UpdateTopStatus()
        {
            bool serialConnected = _serialPort != null && _serialPort.IsOpen;
            if (_demoMode)
            {
                _connectionLabel.Text = "Status: Demo mode is running";
                _connectionLabel.ForeColor = Color.FromArgb(123, 94, 230);
            }
            else if (serialConnected)
            {
                _connectionLabel.Text = "Status: JDY-31 connected";
                _connectionLabel.ForeColor = Color.FromArgb(47, 148, 108);
            }
            else
            {
                _connectionLabel.Text = "Status: Disconnected";
                _connectionLabel.ForeColor = Color.FromArgb(145, 97, 115);
            }

            _packetLabel.Text = "Packets: " + _state.PacketCount;
        }

        // Home page summarizes the most important cross-module signals.
        private void UpdateHomePage()
        {
            bool fresh = _state.HasFreshPacket;
            if (_state.DeviceTime.HasValue)
            {
                _homeTimeLabel.Text = _state.DeviceTime.Value.ToString("HH:mm:ss");
                _homeDateLabel.Text = _state.DeviceTime.Value.ToString("yyyy-MM-dd");
                _homeTimeSourceLabel.Text = fresh ? "Time Source: JDY-31 / DS3231" : "Time Source: JDY-31 (data is stale)";
            }
            else
            {
                _homeTimeLabel.Text = "--:--:--";
                _homeDateLabel.Text = "Waiting for JDY-31 time data";
                _homeTimeSourceLabel.Text = _demoMode ? "Time Source: Demo data" : "Time Source: Not connected";
            }

            _homeConnectionValue.Text = _demoMode ? "Demo" : ((_serialPort != null && _serialPort.IsOpen) ? "Online" : "Offline");
            _homeGpsValue.Text = _state.HasValidPosition ? (_state.PositionSource + " / Fix") : "No Fix";
            _homeHeartValue.Text = FormatPair(_state.HeartRateBpm, "bpm", _state.Spo2Percent, "%");
            _homeLightValue.Text = (_state.LightPercent.HasValue ? _state.LightPercent.Value + "%" : "--") + " / " + (_state.StepCount.HasValue ? _state.StepCount.Value.ToString() : "--");

            _homeOled.SetLines(BuildRtcLines());
        }

        // Temperature page combines raw readings, comfort labels, thresholds, and OLED mirror text.
        private void UpdateTempPage()
        {
            _updatingThresholds = true;
            _tempWarnNumeric.Value = ClampNumeric(_tempWarnNumeric, _state.TemperatureWarn ?? 30);
            _updatingThresholds = false;

            _tempValueLabel.Text = _state.TemperatureC.HasValue ? _state.TemperatureC.Value + " °C" : "--";
            _tempComfortValueLabel.Text = _state.TemperatureComfort;
            _humidityValueLabel.Text = _state.HumidityPercent.HasValue ? _state.HumidityPercent.Value + " %" : "--";
            _humidityComfortValueLabel.Text = _state.HumidityComfort;
            _climateValueLabel.Text = string.IsNullOrWhiteSpace(_state.ClimateComfort) ? "--" : _state.ClimateComfort;
            _tempWarnValueLabel.Text = (_state.TemperatureWarn ?? (int)_tempWarnNumeric.Value) + " °C";
            _tempWarnSourceLabel.Text = _state.LastRawLine.IndexOf("TEMP_WARN=", StringComparison.OrdinalIgnoreCase) >= 0
                ? "The temperature warning threshold is synchronized from the JDY-31 TEMP_WARN field."
                : "Using the local GUI threshold until JDY-31 sends TEMP_WARN. The included firmware patch enables it.";
            _tempOled.SetLines(BuildTempLines());
        }

        // Heart page highlights the validated HR/SpO2 values and finger-detection status.
        private void UpdateHeartPage()
        {
            _updatingThresholds = true;
            _heartWarnNumeric.Value = ClampNumeric(_heartWarnNumeric, _state.HeartRateWarn ?? 120);
            _updatingThresholds = false;

            _hrValueLabel.Text = _state.HeartRateBpm.HasValue ? _state.HeartRateBpm.Value + " bpm" : "--";
            _spo2ValueLabel.Text = _state.Spo2Percent.HasValue ? _state.Spo2Percent.Value + " %" : "--";
            _fingerValueLabel.Text = _state.FingerText;
            _heartWarnValueLabel.Text = (_state.HeartRateWarn ?? (int)_heartWarnNumeric.Value) + " bpm";
            _heartWarnSourceLabel.Text = _state.LastRawLine.IndexOf("HR_WARN=", StringComparison.OrdinalIgnoreCase) >= 0
                ? "The heart-rate warning threshold is synchronized from the JDY-31 HR_WARN field."
                : "Using the local GUI threshold until JDY-31 sends HR_WARN. The included firmware patch enables it.";
            _heartOled.SetLines(BuildHeartLines());
        }

        // GPS page explains both the active position source and the quality/freshness of the current fix.
        private void UpdateGpsPage()
        {
            _gpsSourceValueLabel.Text = string.IsNullOrWhiteSpace(_state.PositionSource) ? "NONE" : _state.PositionSource;
            _gpsFixValueLabel.Text = _state.PositionFix.HasValue ? _state.PositionFix.Value.ToString() : "--";
            _gpsLatValueLabel.Text = string.IsNullOrWhiteSpace(_state.Latitude) ? "--" : _state.Latitude;
            _gpsLonValueLabel.Text = string.IsNullOrWhiteSpace(_state.Longitude) ? "--" : _state.Longitude;
            _gpsFreshValueLabel.Text = _state.HasValidPosition && _state.HasFreshPacket
                ? "Position is valid and fresh data has arrived within the last 5 seconds."
                : "No fresh position data. The current firmware will show the 'Send NMEA over BT' prompt.";
            _gpsOled.SetLines(BuildGpsLines());
        }

        // Light page keeps both the percentage and the raw ADC value visible for debugging/calibration.
        private void UpdateLightPage()
        {
            _adcValueLabel.Text = _state.AdcRaw.HasValue ? _state.AdcRaw.Value.ToString() : "--";
            _lightPercentValueLabel.Text = _state.LightPercent.HasValue ? _state.LightPercent.Value + " %" : "--";
            _lightLevelValueLabel.Text = _state.LightLevel;
            _lightExtraNoteLabel.Text = _state.AdcRaw.HasValue
                ? "Integrated light telemetry is active: LIGHT and ADC_RAW are both available for the OLED mirror."
                : "Waiting for ADC_RAW from JDY-31. The included firmware patch enables the complete light page telemetry.";
            _lightOled.SetLines(BuildLightLines());
        }

        // Motion page combines step count, Euler angles, and raw IMU vectors.
        private void UpdateMotionPage()
        {
            _stepValueLabel.Text = _state.StepCount.HasValue ? _state.StepCount.Value.ToString() : "--";
            _rollValueLabel.Text = _state.Roll.HasValue ? _state.Roll.Value.ToString() : "--";
            _pitchValueLabel.Text = _state.Pitch.HasValue ? _state.Pitch.Value.ToString() : "--";
            _yawValueLabel.Text = _state.Yaw.HasValue ? _state.Yaw.Value.ToString() : "--";
            _motionExtraNoteLabel.Text = HasVectorData(_state.AccelX, _state.AccelY, _state.AccelZ)
                ? FormatVectorText("AX", _state.AccelX, "AY", _state.AccelY, "AZ", _state.AccelZ)
                : "Waiting for AX / AY / AZ from the integrated motion extension.";
            _motionGyroValueLabel.Text = HasVectorData(_state.GyroX, _state.GyroY, _state.GyroZ)
                ? FormatVectorText("GX", _state.GyroX, "GY", _state.GyroY, "GZ", _state.GyroZ)
                : "Waiting for GX / GY / GZ from the integrated motion extension.";
            _motionOled.SetLines(BuildMotionLines());
        }

        // Clamp values received from telemetry before assigning them back to WinForms numeric controls.
        private decimal ClampNumeric(NumericUpDown control, int value)
        {
            if (value < control.Minimum)
            {
                return control.Minimum;
            }
            if (value > control.Maximum)
            {
                return control.Maximum;
            }
            return value;
        }

        // Ignore events triggered while the control is being synchronized from telemetry.
        private void TempWarnNumeric_ValueChanged(object sender, EventArgs e)
        {
            if (_updatingThresholds)
            {
                return;
            }

            _state.TemperatureWarn = (int)_tempWarnNumeric.Value;
            UpdateUiFromState();
        }

        // Ignore events triggered while the control is being synchronized from telemetry.
        private void HeartWarnNumeric_ValueChanged(object sender, EventArgs e)
        {
            if (_updatingThresholds)
            {
                return;
            }

            _state.HeartRateWarn = (int)_heartWarnNumeric.Value;
            UpdateUiFromState();
        }

        // The Build*Lines helpers intentionally mirror the 8-row layout used on the physical OLED.
        private string[] BuildRtcLines()
        {
            string[] lines = EmptyOledLines();
            lines[0] = OledText("RTC");
            if (_state.DeviceTime.HasValue)
            {
                lines[2] = OledText(_state.DeviceTime.Value.ToString("yyyy-MM-dd"));
                lines[4] = OledText(_state.DeviceTime.Value.ToString("HH:mm:ss"));
            }
            else
            {
                lines[2] = OledText("RTC NOT READY");
            }
            return lines;
        }

        private string[] BuildTempLines()
        {
            string[] lines = EmptyOledLines();
            lines[0] = OledText("Temp / Humi");
            lines[2] = OledText("Temp:" + (_state.TemperatureC.HasValue ? _state.TemperatureC.Value.ToString().PadLeft(2) + "C" : "--C"));
            lines[3] = OledText("Temp:" + _state.TemperatureComfort);
            lines[5] = OledText("Humi:" + (_state.HumidityPercent.HasValue ? _state.HumidityPercent.Value.ToString().PadLeft(2) + "%" : "--%"));
            lines[6] = OledText("Hum :" + _state.HumidityComfort);
            lines[7] = OledText("Warn:" + (_state.TemperatureWarn ?? 30).ToString().PadLeft(2) + "C");
            return lines;
        }

        private string[] BuildHeartLines()
        {
            string[] lines = EmptyOledLines();
            lines[0] = OledText("Heart Rate & SPO2");
            lines[2] = OledText(_state.Spo2Percent.HasValue ? string.Format("SPO2:{0,3}%", _state.Spo2Percent.Value) : "SPO2:---%");
            lines[4] = OledText(_state.HeartRateBpm.HasValue ? string.Format("HR:{0,3}bpm", _state.HeartRateBpm.Value) : "HR:---bpm");
            lines[6] = OledText(string.Format("Warn:{0,3}", _state.HeartRateWarn ?? 120));
            lines[7] = OledText("Finger:" + _state.FingerText);
            return lines;
        }

        private string[] BuildGpsLines()
        {
            string[] lines = EmptyOledLines();
            lines[0] = OledText("GPS / BT");
            if (_state.HasValidPosition)
            {
                lines[2] = OledText("SRC:" + (string.IsNullOrWhiteSpace(_state.PositionSource) ? "UNK" : _state.PositionSource) + " FIX");
                lines[4] = OledText("Lat:" + _state.Latitude);
                lines[6] = OledText("Lon:" + _state.Longitude);
            }
            else
            {
                lines[2] = OledText("SRC:None");
                lines[4] = OledText("Lat:--");
                lines[6] = OledText("Lon:--");
                lines[7] = OledText("Send NMEA over BT");
            }
            return lines;
        }

        private string[] BuildLightLines()
        {
            string[] lines = EmptyOledLines();
            lines[0] = OledText("Light Sensor");
            lines[2] = OledText(_state.AdcRaw.HasValue ? string.Format("ADC:{0,4}", _state.AdcRaw.Value) : "ADC:----");
            lines[4] = OledText(_state.LightPercent.HasValue ? string.Format("Light:{0,3}%", _state.LightPercent.Value) : "Light:--%");
            lines[6] = OledText("Level:" + _state.LightLevel);
            return lines;
        }

        private string[] BuildMotionLines()
        {
            string[] lines = EmptyOledLines();
            lines[0] = OledText("Motion");
            lines[2] = OledText("Step:" + ((_state.StepCount ?? 0).ToString().PadLeft(5, '0')));
            if (_state.Roll.HasValue || _state.Pitch.HasValue || _state.Yaw.HasValue)
            {
                lines[4] = OledText("Roll:" + FormatAngle(_state.Roll));
                lines[5] = OledText("Pitch:" + FormatAngle(_state.Pitch));
                lines[6] = OledText("Yaw :" + FormatAngle(_state.Yaw));
            }
            else
            {
                lines[4] = OledText("Roll:N/A");
                lines[5] = OledText("Pitch:N/A");
                lines[6] = OledText("Yaw :N/A");
            }
            return lines;
        }

        // Keep angle fields aligned so the OLED preview looks close to the fixed-width embedded rendering.
        private static string FormatAngle(int? value)
        {
            return value.HasValue ? value.Value.ToString().PadLeft(4) : " N/A";
        }

        private static string[] EmptyOledLines()
        {
            return new[] { "", "", "", "", "", "", "", "" };
        }

        private static string OledText(string text)
        {
            if (string.IsNullOrEmpty(text))
            {
                return string.Empty;
            }

            return text.Length > 21 ? text.Substring(0, 21) : text;
        }

        private static string FormatPair(int? leftValue, string leftUnit, int? rightValue, string rightUnit)
        {
            string left = leftValue.HasValue ? leftValue.Value + leftUnit : "--";
            string right = rightValue.HasValue ? rightValue.Value + rightUnit : "--";
            return left + " / " + right;
        }

        private static bool HasVectorData(int? first, int? second, int? third)
        {
            return first.HasValue || second.HasValue || third.HasValue;
        }

        private static string FormatVectorText(string firstName, int? first, string secondName, int? second, string thirdName, int? third)
        {
            return string.Format("{0}={1}   {2}={3}   {4}={5}",
                firstName,
                FormatOptionalInt(first),
                secondName,
                FormatOptionalInt(second),
                thirdName,
                FormatOptionalInt(third));
        }

        private static string FormatOptionalInt(int? value)
        {
            return value.HasValue ? value.Value.ToString() : "--";
        }

        // Append telemetry and parser messages while trimming the log to a manageable size.
        private void AppendLog(string line)
        {
            string timestamped = DateTime.Now.ToString("HH:mm:ss") + "  " + line;
            _rawLogBox.AppendText(timestamped + Environment.NewLine);

            const int maxLength = 18000;
            if (_rawLogBox.TextLength > maxLength)
            {
                _rawLogBox.Text = _rawLogBox.Text.Substring(_rawLogBox.TextLength - maxLength);
                _rawLogBox.SelectionStart = _rawLogBox.TextLength;
            }

            _rawLogBox.SelectionStart = _rawLogBox.TextLength;
            _rawLogBox.ScrollToCaret();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            // Stop timers and release the serial port before the process exits.
            _uiTimer.Stop();
            _demoTimer.Stop();
            DisconnectSerial();
            base.OnFormClosing(e);
        }

        private static System.Drawing.Drawing2D.GraphicsPath CreateRoundedRectangle(Rectangle bounds, int radius)
        {
            int diameter = Math.Max(2, radius * 2);
            System.Drawing.Drawing2D.GraphicsPath path = new System.Drawing.Drawing2D.GraphicsPath();
            path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180, 90);
            path.AddArc(bounds.Right - diameter, bounds.Y, diameter, diameter, 270, 90);
            path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
            path.AddArc(bounds.X, bounds.Bottom - diameter, diameter, diameter, 90, 90);
            path.CloseFigure();
            return path;
        }
    }
}
