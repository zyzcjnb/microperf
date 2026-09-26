# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

FROM --platform=linux/amd64 python

RUN mkdir /service
COPY transaction-executor/ /service/transaction-executor/
WORKDIR /service/transaction-executor
RUN python -m pip install --upgrade pip
RUN python -m pip install -r requirements.txt

EXPOSE 50052

ENTRYPOINT ["python", "app.py"]
