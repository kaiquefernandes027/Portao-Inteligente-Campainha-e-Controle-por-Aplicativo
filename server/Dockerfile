FROM node:24-bookworm-slim
WORKDIR /app
COPY package.json ./
RUN npm install --omit=dev && mkdir -p /app/data && chown -R node:node /app
COPY index.mjs server.mjs push.mjs ./
USER node
ENV NODE_ENV=production HOST=0.0.0.0 PORT=3000 DB_PATH=/app/data/portao.sqlite
EXPOSE 3000
CMD ["node", "index.mjs"]
