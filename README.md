# Tic Tac Toe Online

Game Tic Tac Toe trực tuyến với chế độ chơi đối kháng với AI hoặc chơi với người khác thông qua kết nối WebSocket.

## Tính năng

- Chơi đối kháng với AI (3 cấp độ: dễ, trung bình, khó)
- Chơi trực tuyến với người khác
- Hệ thống matchmaking tự động
- Giao diện người dùng trực quan và thân thiện

## Demo

Bạn có thể chơi game trực tuyến tại: [https://yourusername.github.io/tic-tac-toe-online](https://yourusername.github.io/tic-tac-toe-online)

## Cài đặt và chạy cục bộ

### Yêu cầu tiên quyết

- Node.js (khuyến nghị phiên bản 14.x trở lên)
- npm (đi kèm với Node.js)

### Các bước cài đặt

1. Clone dự án về máy:

```bash
git clone https://github.com/yourusername/tic-tac-toe-online.git
cd tic-tac-toe-online
```

2. Cài đặt các dependencies:

```bash
npm install
```

3. Chạy ứng dụng:

```bash
npm start
```

4. Mở trình duyệt và truy cập:

```
http://localhost:5000
```

## Triển khai lên GitHub Pages

### Sử dụng GitHub Pages cho phiên bản tĩnh (chỉ chơi với AI)

1. Tạo một repository mới trên GitHub

2. Push code của bạn lên repository:

```bash
git init
git add .
git commit -m "Initial commit"
git branch -M main
git remote add origin https://github.com/yourusername/tic-tac-toe-online.git
git push -u origin main
```

3. Vào Settings của repository, kéo xuống phần "GitHub Pages"

4. Chọn "Branch: main" và thư mục "/root" (hoặc "/docs" nếu bạn đặt file HTML trong thư mục docs) và click "Save"

5. Sau vài phút, trang web của bạn sẽ được triển khai tại `https://yourusername.github.io/tic-tac-toe-online`

### Triển khai phiên bản đầy đủ với WebSocket server

Để triển khai phiên bản có hỗ trợ chơi trực tuyến, bạn cần một máy chủ thực sự. Dưới đây là cách triển khai lên Heroku:

1. Tạo tài khoản trên [Heroku](https://www.heroku.com/)

2. Cài đặt Heroku CLI:
   ```bash
   npm install -g heroku
   ```

3. Đăng nhập Heroku từ terminal:
   ```bash
   heroku login
   ```

4. Tạo một ứng dụng mới trên Heroku:
   ```bash
   heroku create your-app-name
   ```

5. Push code lên Heroku:
   ```bash
   git push heroku main
   ```

6. Mở ứng dụng:
   ```bash
   heroku open
   ```

## Cách chơi

1. Truy cập vào game qua URL
2. Chọn chế độ chơi:
   - **vs AI**: Chơi với máy tính
   - **Online Multiplayer**: Chơi với người khác trực tuyến
3. Nếu chọn chế độ vs AI:
   - Chọn mức độ khó (Dễ, Trung bình, Khó)
   - Bắt đầu chơi bằng cách đánh X vào ô trống
4. Nếu chọn chế độ Online Multiplayer:
   - Nhập tên người dùng và nhấn "Connect"
   - Nhấn "Find Match" để tìm đối thủ
   - Khi tìm thấy đối thủ, trò chơi sẽ tự động bắt đầu

## Công nghệ sử dụng

- Frontend: HTML, CSS, JavaScript
- Backend: Node.js, Express
- Kết nối realtime: WebSocket (ws)
- Database: MongoDB (tùy chọn, không bắt buộc)

## Đóng góp

Mọi đóng góp đều được chào đón! Nếu bạn muốn đóng góp, hãy:

1. Fork dự án
2. Tạo nhánh tính năng (`git checkout -b feature/amazing-feature`)
3. Commit thay đổi của bạn (`git commit -m 'Add some amazing feature'`)
4. Push lên nhánh (`git push origin feature/amazing-feature`)
5. Mở Pull Request

## Giấy phép

Dự án này được cấp phép theo Giấy phép MIT - xem tệp [LICENSE](LICENSE) để biết chi tiết.

## Liên hệ

Nếu bạn có bất kỳ câu hỏi nào, vui lòng mở một issue trong repository này. 