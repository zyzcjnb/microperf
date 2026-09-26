#!/usr/bin/env bash

# set -x

# echo "arg 1 $1"


BASE_DIR=/usr/share/nginx/html

if [ -n "$1" ]
then
    exec "$@"
fi

if [ -n "$monitoring_EUM_KEY" -a -n "$monitoring_EUM_REPORTING_URL" ]
then
    echo "Enabling monitoring EUM"
    result=$(curl -kv -s --connect-timeout 10 "$monitoring_EUM_REPORTING_URL" 2>&1 | grep "301 Moved Permanently")
    if [ -n "$result" ]; 
    then
        echo '301 Moved Permanently found!'
        [[ "${monitoring_EUM_REPORTING_URL}" != */ ]] &&  monitoring_EUM_REPORTING_URL="${monitoring_EUM_REPORTING_URL}/"
        sed -i "s|monitoring_EUM_KEY|$monitoring_EUM_KEY|" $BASE_DIR/eum-tmpl.html
        sed -i "s|monitoring_EUM_REPORTING_URL|$monitoring_EUM_REPORTING_URL|" $BASE_DIR/eum-tmpl.html
        cp $BASE_DIR/eum-tmpl.html $BASE_DIR/eum.html
    else
        echo "Go with the user input"
        sed -i "s|monitoring_EUM_KEY|$monitoring_EUM_KEY|" $BASE_DIR/eum-tmpl.html
        sed -i "s|monitoring_EUM_REPORTING_URL|$monitoring_EUM_REPORTING_URL|" $BASE_DIR/eum-tmpl.html
        cp $BASE_DIR/eum-tmpl.html $BASE_DIR/eum.html
    fi

else
    echo "EUM not enabled"
    cp $BASE_DIR/empty.html $BASE_DIR/eum.html
fi

# make sure nginx can access the eum file
chmod 644 $BASE_DIR/eum.html

# apply environment variables to default.conf
envsubst '${CATALOGUE_HOST} ${USER_HOST} ${CART_HOST} ${SHIPPING_HOST} ${PAYMENT_HOST} ${RATINGS_HOST}' < /etc/nginx/conf.d/default.conf.template > /etc/nginx/conf.d/default.conf

if [ -f /tmp/ngx_http_opentracing_module.so -a -f /tmp/libmonitoring_sensor.so ]
then
    echo "Patching for monitoring tracing"
    mv /tmp/ngx_http_opentracing_module.so /usr/lib/nginx/modules
    mv /tmp/libmonitoring_sensor.so /usr/local/lib
    cat - /etc/nginx/nginx.conf << !EOF! > /tmp/nginx.conf
# Extra configuration for monitoring tracing
load_module modules/ngx_http_opentracing_module.so;

# Pass through these env vars
env monitoring_SERVICE_NAME;
env monitoring_AGENT_HOST;
env monitoring_AGENT_PORT;
env monitoring_MAX_BUFFERED_SPANS;
env monitoring_DEV;
!EOF!

    mv /tmp/nginx.conf /etc/nginx/nginx.conf
    echo "{}" > /etc/monitoring-config.json
else
    echo "Tracing not enabled"
    # remove tracing config
    sed -i '1,3d' /etc/nginx/conf.d/default.conf
fi

exec nginx-debug -g "daemon off;"

