<?php

function materialized_view_status() {
  $content = array();

  $content[] = drupal_get_form('materialized_view_admin_ops_form');

  return implode("\n", $content);
}

function materialized_view_admin_ops_form() {
  $form = array();
  
  $form['reconcile'] = array(
    '#type' => 'submit',
    '#value' => t('Reconcile now'),
  );
  
  return $form;
}

function materialized_view_admin_ops_form_submit($form, &$form_state) {
  $op = $form_state['clicked_button']['#value'];
  
  if ($op == t('Reconcile now')) {
    materialized_view_reconcile();
    drupal_set_message('Materialized views reconciled.');
  }
}
