using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // Reusable surface that can draw both a soft gradient wash and an optional background image.
    public sealed class GradientPanel : Panel
    {
        public Color ColorA { get; set; }
        public Color ColorB { get; set; }
        public LinearGradientMode GradientMode { get; set; }
        public Image BackgroundTexture { get; set; }
        public ImageLayout BackgroundTextureLayout { get; set; }

        public GradientPanel()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            DoubleBuffered = true;
            ResizeRedraw = true;
            BackColor = Color.Transparent;
            ColorA = Color.FromArgb(255, 239, 248);
            ColorB = Color.FromArgb(237, 244, 255);
            GradientMode = LinearGradientMode.ForwardDiagonal;
            BackgroundTextureLayout = ImageLayout.Stretch;
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            // Paint the texture first and then blend the gradient overlay on top of it.
            base.OnPaintBackground(e);

            Rectangle rect = ClientRectangle;
            if (rect.Width <= 0 || rect.Height <= 0)
            {
                return;
            }

            if (BackgroundTexture != null)
            {
                e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                e.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                Rectangle imageRect = CalculateImageBounds(rect, BackgroundTexture.Size, BackgroundTextureLayout);
                e.Graphics.DrawImage(BackgroundTexture, imageRect);
            }

            if (ColorA.A > 0 || ColorB.A > 0)
            {
                using (LinearGradientBrush brush = new LinearGradientBrush(rect, ColorA, ColorB, GradientMode))
                {
                    e.Graphics.FillRectangle(brush, rect);
                }
            }
        }

        // Match WinForms-style image layout rules so the same texture can be reused across different container sizes.
        private static Rectangle CalculateImageBounds(Rectangle bounds, Size imageSize, ImageLayout layout)
        {
            if (imageSize.Width <= 0 || imageSize.Height <= 0)
            {
                return bounds;
            }

            switch (layout)
            {
                case ImageLayout.None:
                    return new Rectangle(bounds.Location, imageSize);

                case ImageLayout.Center:
                    return new Rectangle(
                        bounds.X + (bounds.Width - imageSize.Width) / 2,
                        bounds.Y + (bounds.Height - imageSize.Height) / 2,
                        imageSize.Width,
                        imageSize.Height);

                case ImageLayout.Zoom:
                    float zoomRatio = Math.Min((float)bounds.Width / imageSize.Width, (float)bounds.Height / imageSize.Height);
                    int zoomWidth = Math.Max(1, (int)Math.Round(imageSize.Width * zoomRatio));
                    int zoomHeight = Math.Max(1, (int)Math.Round(imageSize.Height * zoomRatio));
                    return new Rectangle(
                        bounds.X + (bounds.Width - zoomWidth) / 2,
                        bounds.Y + (bounds.Height - zoomHeight) / 2,
                        zoomWidth,
                        zoomHeight);

                case ImageLayout.Stretch:
                default:
                    return bounds;
            }
        }
    }
}
